// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotPyramid.hpp"

#include "PlotLevels.hpp"

#include <QSemaphore>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gui {

namespace {

/// `value` rounded up to a power of two, at least one.
[[nodiscard]] long long roundUpPowerOfTwo(long long value)
{
    long long power = 1;
    while (power < value && power < (1LL << 40)) {
        power <<= 1;
    }
    return power;
}

/// Doubles one piece of a fold is worth spreading over a thread.
///
/// Below this the pool costs more than the fold does: posting a task, waking a
/// thread and waiting on a semaphore is a few microseconds, and a fold of a few
/// thousand doubles is less than that. So a frame's own fold stays on the thread
/// that asked for it -- which is what keeps it microseconds -- and only the
/// passes over a whole line are spread.
constexpr long long kFoldGrain = 1 << 16;

/// Run `body(from, to)` over `[0, units)`, in parallel when it is worth it.
///
/// `work` is how many doubles the whole of it walks, and it is a separate
/// argument because it is not `units`: a fold answers with one bucket per unit
/// and reads `work / units` elements to find each one, so a summary of ten
/// million elements into two thousand buckets is two thousand units and ten
/// million doubles of work. Splitting on the units would leave that one on this
/// thread, which is the one fold large enough that it must not be.
template<typename F>
void overRanges(long long units, long long work, F body)
{
    const int threads = std::max(QThreadPool::globalInstance()->maxThreadCount(), 1);
    const long long pieces = std::min<long long>(std::min<long long>(threads, units),
                                                 std::max<long long>(1, work / kFoldGrain));
    if (pieces <= 1) {
        body(0, units);
        return;
    }
    const long long each = (units + pieces - 1) / pieces;
    QSemaphore done;
    long long started = 0;
    for (long long piece = 0; piece < pieces; ++piece) {
        const long long from = piece * each;
        if (from >= units) {
            break;
        }
        const long long to = std::min(from + each, units);
        ++started;
        QThreadPool::globalInstance()->start([&done, &body, from, to] {
            body(from, to);
            done.release();
        });
    }
    // Acquired rather than left to the pool's own wait: this blocks until every
    // piece is finished, which is what makes `body` safe to capture by
    // reference and the output safe to read on return.
    done.acquire(static_cast<int>(started));
}

} // namespace

std::size_t LinePyramid::doubles() const
{
    std::size_t total = 0;
    for (const PyramidLevel& level : levels) {
        total += level.values.size();
    }
    return total;
}

long long pyramidDoubles(long long length, long long base)
{
    if (length <= 0) {
        return 0;
    }
    const long long bucket = std::max<long long>(base, 1);
    const long long size = bucket == 1 ? length : 2 * ((length + bucket - 1) / bucket);
    // The levels above add a quarter, a sixteenth, a sixty-fourth... which sums
    // to a third. Rounded up, because the top few levels are a handful of
    // doubles each and there are more of them than the series accounts for.
    return size + size / 3 + 64;
}

long long baseBucketFor(long long length, long long budget)
{
    if (length <= 0) {
        return 1;
    }
    if (budget <= 0) {
        return roundUpPowerOfTwo(length); // one bucket: the cheapest pyramid there is
    }
    // Doubled until it fits, rather than solved: a pyramid's cost is not quite
    // a closed form -- the levels above the base are a rounded-up geometric sum
    // -- and there are at most forty steps of this for any line that exists.
    long long base = 1;
    while (pyramidDoubles(length, base) > budget && base < length) {
        base <<= 1;
    }
    return base;
}

bool coarsenTo(LinePyramid& pyramid, long long base)
{
    std::size_t drop = 0;
    while (drop + 1 < pyramid.levels.size() && pyramid.levels[drop].bucket < base) {
        ++drop;
    }
    if (drop == 0) {
        return false;
    }
    pyramid.levels.erase(pyramid.levels.begin(),
                         pyramid.levels.begin() + static_cast<long>(drop));
    // The vectors that went are the whole point, so the capacity goes with
    // them: a std::vector erased out of the front of another leaves the
    // survivors where they were, but the elements removed are destroyed and
    // their buffers freed, which is the memory the reader asked for back.
    pyramid.levels.shrink_to_fit();
    return true;
}

bool fitToBudget(LinePyramid& pyramid, long long budget)
{
    const long long wanted = baseBucketFor(pyramid.length, budget);
    if (wanted < pyramid.baseBucket()) {
        return false;
    }
    coarsenTo(pyramid, wanted);
    return true;
}

void buildLevels(LinePyramid& pyramid)
{
    if (pyramid.levels.empty()) {
        return;
    }
    constexpr long long factor = 1LL << kPyramidOctaves;

    while (pyramid.levels.back().buckets() > 1) {
        const PyramidLevel& below = pyramid.levels.back();
        PyramidLevel level;
        level.bucket = below.bucket * factor;

        const long long buckets = (below.buckets() + factor - 1) / factor;
        level.values.resize(static_cast<std::size_t>(buckets) * 2);

        const double* source = below.values.data();
        const long long sourceBuckets = below.buckets();
        const bool raw = below.bucket == 1;
        double* out = level.values.data();

        // Every output bucket is a fold of `factor` input buckets, and no two
        // of them share an input -- so the ranges are disjoint and there is
        // nothing to lock.
        overRanges(buckets, static_cast<long long>(below.values.size()),
                   [&](long long from, long long to) {
                       const long long first = from * factor;
                       const long long count = std::min(to * factor, sourceBuckets) - first;
                       if (count <= 0) {
                           return;
                       }
                       if (raw) {
                           reduceBucketsInto(source + first, count, factor, out + from * 2);
                       }
                       else {
                           coarsenEnvelopeInto(source + first * 2, count, factor, out + from * 2);
                       }
                   });

        pyramid.levels.push_back(std::move(level));
        if (pyramid.levels.size() > 64) {
            break; // a line no dataset has; the loop must still end
        }
    }
}

PyramidBuilder::PyramidBuilder(long long length, long long base)
    : base_(std::max<long long>(base, 1))
{
    pyramid_.length = std::max<long long>(length, 0);
    PyramidLevel bottom;
    bottom.bucket = base_;
    // Reserved and appended to rather than sized and overwritten: sizing it
    // writes the whole buffer once before a single element has been read, which
    // on a large line is hundreds of megabytes of zeroes and the page faults
    // that come with first touching them.
    const long long buckets = (pyramid_.length + base_ - 1) / base_;
    bottom.values.reserve(static_cast<std::size_t>(base_ == 1 ? pyramid_.length : buckets * 2));
    pyramid_.levels.push_back(std::move(bottom));
}

void PyramidBuilder::add(const double* values, long long count)
{
    if (values == nullptr || count <= 0) {
        return;
    }
    taken_ += count;
    std::vector<double>& bottom = pyramid_.levels.front().values;

    const auto fold = [&](const double* from, long long many) {
        if (base_ == 1) {
            bottom.insert(bottom.end(), from, from + many);
        }
        else {
            reduceBuckets(from, many, base_, bottom);
        }
    };

    if (carry_.empty() && count % base_ == 0) {
        // Straight out of the read, which is every read but the last: kReadRun
        // is a power of two and so is the base. The carry below is for the ones
        // that are not -- a scattered subscript breaking the run, or the tail
        // of the line -- and going through it otherwise would be a second copy
        // of every element.
        fold(values, count);
    }
    else {
        carry_.insert(carry_.end(), values, values + count);
        const long long whole = (static_cast<long long>(carry_.size()) / base_) * base_;
        if (whole > 0) {
            fold(carry_.data(), whole);
            carry_.erase(carry_.begin(), carry_.begin() + whole);
        }
    }
}

LinePyramid PyramidBuilder::finish()
{
    if (!carry_.empty()) {
        std::vector<double>& bottom = pyramid_.levels.front().values;
        if (base_ == 1) {
            bottom.insert(bottom.end(), carry_.begin(), carry_.end());
        }
        else {
            reduceBuckets(carry_.data(), static_cast<long long>(carry_.size()), base_, bottom);
        }
        carry_.clear();
    }
    // The length is what was handed in, not what was promised. Everything that
    // reads a pyramid -- a fold, a column, the extremes over a run -- indexes
    // its base up to `length` and trusts the base to be that long, so a line
    // whose reads stopped short of the length it was built for would otherwise
    // be read past the end of the buffer holding it. The caller that stops
    // early on purpose used to have to say so by hand afterwards; now nothing
    // can forget to.
    pyramid_.length = std::min(pyramid_.length, taken_);
    // The levels above the base are built here, whole and over the machine,
    // rather than a chunk at a time as each read lands.
    //
    // Folding them chunk by chunk was tried, on the argument that a level made
    // out of a few kilobytes still in cache beats one made out of a re-read of
    // the whole base. It is half again as slow, and measurably: the fold is
    // *compute* bound, not memory bound -- extremesOf() tests every element for
    // being finite and branches on it, which neither vectorises nor prefetches
    // away -- so what the ladder costs is sixteen million of those and what
    // decides it is how many cores are doing them. Chunk by chunk that is one.
    buildLevels(pyramid_);
    return std::move(pyramid_);
}

LinePyramid pyramidOf(const double* values, long long count, long long base)
{
    LinePyramid pyramid;
    if (values == nullptr || count <= 0) {
        return pyramid;
    }
    pyramid.length = count;

    PyramidLevel bottom;
    bottom.bucket = std::max<long long>(base, 1);
    if (bottom.bucket == 1) {
        bottom.values.assign(values, values + count);
    }
    else {
        const long long buckets = (count + bottom.bucket - 1) / bottom.bucket;
        bottom.values.resize(static_cast<std::size_t>(buckets) * 2);
        double* out = bottom.values.data();
        const long long width = bottom.bucket;
        overRanges(buckets, count, [&](long long from, long long to) {
            const long long first = from * width;
            const long long take = std::min(to * width, count) - first;
            if (take > 0) {
                reduceBucketsInto(values + first, take, width, out + from * 2);
            }
        });
    }
    pyramid.levels.push_back(std::move(bottom));

    buildLevels(pyramid);
    return pyramid;
}

namespace {

/// The coarsest level a bucket of `bucket` can be folded out of, or -1.
///
/// Coarsest, because it is the least work: a level four times finer than the
/// one asked for is four times the doubles to walk. But it has to *divide*
/// `bucket` and not merely be no larger than it -- a fold of a level whose
/// buckets straddle the wanted boundaries would answer about the wrong
/// elements and drift further out the longer the line.
///
/// Every bucket the zoom asks for is a power of two and every level is a power
/// of four, so there is always one within a factor of two. The whole-line
/// summary is the case that is not: its stride is `ceil(length / buckets)` and
/// can be any integer at all, so it usually lands on the base and pays a pass
/// over the whole line for it. That is once per pane width, not once per frame.
[[nodiscard]] int levelFor(const LinePyramid& pyramid, long long bucket)
{
    int best = -1;
    for (std::size_t i = 0; i < pyramid.levels.size(); ++i) {
        if (pyramid.levels[i].bucket > bucket) {
            break; // finest first, so everything past this one is coarser still
        }
        if (bucket % pyramid.levels[i].bucket == 0) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

/// Where slot `slot` of bucket `bucket` at `levelIndex` occurred.
///
/// An extreme of a union is an extreme of one of its children, so the walk
/// opens that one child and not the bucket. Four comparisons a level, down to
/// the base. At bucket one the index is the sample. At a coarser base there is
/// nothing finer to open, and the answer is the bucket's start or its middle --
/// the stand-in the pair has always been drawn at, and no worse than that.
[[nodiscard]] long long locateSlot(const LinePyramid& pyramid, int levelIndex, long long bucket,
                                   int slot)
{
    const PyramidLevel& level = pyramid.levels[static_cast<std::size_t>(levelIndex)];
    const long long origin = bucket * level.bucket;
    const long long end = std::min(origin + level.bucket, pyramid.length);
    if (end <= origin) {
        return std::max<long long>(origin, 0);
    }
    if (level.bucket == 1) {
        return origin;
    }
    const double* pair = level.values.data() + static_cast<std::size_t>(bucket) * 2;
    const double want = pair[slot];
    if (!std::isfinite(want)) {
        return origin;
    }
    // Both slots the same value: the first finite sample, which is the first
    // of the pair. A coarser base cannot say which sample, so both land on
    // the start rather than one of them on the middle.
    const auto coarse = [&]() {
        if (slot == 0 || pair[0] == pair[1]) {
            return origin;
        }
        return std::min(origin + level.bucket / 2, end - 1);
    };
    if (levelIndex == 0) {
        return coarse();
    }
    const PyramidLevel& finer = pyramid.levels[static_cast<std::size_t>(levelIndex - 1)];
    if (finer.bucket == 1) {
        const long long n = end - origin;
        const double* raw = finer.values.data();
        for (long long i = 0; i < n; ++i) {
            if (raw[static_cast<std::size_t>(origin + i)] == want) {
                return origin + i;
            }
        }
        return origin;
    }
    const long long factor = level.bucket / finer.bucket;
    const long long child0 = bucket * factor;
    const long long childCount =
        std::max<long long>(0, std::min(factor, finer.buckets() - child0));
    for (long long c = 0; c < childCount; ++c) {
        const double* child = finer.values.data() + static_cast<std::size_t>(child0 + c) * 2;
        // The earlier slot first. The value was copied up, so equality is
        // the bit the child holds, and the first match is the first occurrence.
        if (child[0] == want) {
            return locateSlot(pyramid, levelIndex - 1, child0 + c, 0);
        }
        if (child[1] == want) {
            return locateSlot(pyramid, levelIndex - 1, child0 + c, 1);
        }
    }
    return coarse();
}

/// Where `value`, an extreme of source buckets `[begin, begin + count)`, sat.
[[nodiscard]] long long positionOf(const LinePyramid& pyramid, int levelIndex, long long begin,
                                   long long count, double value, long long fallback)
{
    if (!std::isfinite(value) || count <= 0) {
        return fallback;
    }
    const PyramidLevel& level = pyramid.levels[static_cast<std::size_t>(levelIndex)];
    if (level.bucket == 1) {
        return fallback;
    }
    const long long last = std::min(begin + count, level.buckets());
    for (long long b = begin; b < last; ++b) {
        const double* pair = level.values.data() + static_cast<std::size_t>(b) * 2;
        if (pair[0] == value) {
            return locateSlot(pyramid, levelIndex, b, 0);
        }
        if (pair[1] == value) {
            return locateSlot(pyramid, levelIndex, b, 1);
        }
    }
    return fallback;
}

/// Fold `[first, first + span)` of the line at `bucket`, out of the pyramid.
///
/// One bucket as a column: the sample it is entered on, the two extremes, and
/// — when `keepExit` — the sample it is left on, in the order of their positions.
///
/// The sample a bucket is left on and the sample the next one is entered on
/// are neighbours, one index apart. The next column writes that entry, so the
/// exit is a second copy of the same seam. It stays only for the last column,
/// and where the next entry is not a number: the stroke has to end on the last
/// finite sample, or the gap would open a step early. An extreme that sits on
/// the exit is written with the extremes, whichever way the seam goes.
///
/// A seam that is not finite is left out. That is a gap, which is what a NaN
/// already is. A station that is the same sample as one already kept is left
/// out too, so a flank whose extreme sits on the seam stays the two points it
/// always was.
void appendColumn(std::vector<double>& values, std::vector<double>& positions, const double* raw,
                  long long rawCount, long long start, long long end, double earlier,
                  double earlierAt, double later, double laterAt, bool keepExit)
{
    if (!std::isfinite(earlier) && !std::isfinite(later)) {
        const auto nothing = std::numeric_limits<double>::quiet_NaN();
        const double at = static_cast<double>(std::max(start, 0LL));
        values.push_back(nothing);
        positions.push_back(at);
        values.push_back(nothing);
        positions.push_back(at);
        return;
    }
    struct Station
    {
        double at = 0.0;
        double value = 0.0;
    };
    Station stations[4];
    int count = 0;
    const auto add = [&](double at, double value) {
        if (!std::isfinite(at) || !std::isfinite(value)) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            if (stations[i].at == at) {
                return;
            }
        }
        stations[count++] = {at, value};
    };
    if (raw != nullptr && start >= 0 && start < rawCount && std::isfinite(raw[start])) {
        add(static_cast<double>(start), raw[start]);
    }
    add(earlierAt, earlier);
    add(laterAt, later);
    if (keepExit && raw != nullptr && end != start && end >= 0 && end < rawCount &&
        std::isfinite(raw[end])) {
        add(static_cast<double>(end), raw[end]);
    }
    std::sort(stations, stations + count,
              [](const Station& a, const Station& b) { return a.at < b.at; });
    for (int i = 0; i < count; ++i) {
        values.push_back(stations[i].value);
        positions.push_back(stations[i].at);
    }
}

/// The seams beside the extremes, when the base still holds every sample.
///
/// Two indexed reads a bucket, not a second walk: the fold has already found
/// the extremes, and the seam is the sample that walk started and ended on.
/// A coarser base has thrown that sample away. Coarsening an envelope cannot
/// grow it back, so those lines stay the pair.
void withSeams(const LinePyramid& pyramid, long long bucket, long long first, long long count,
               std::vector<double>& values, std::vector<double>& positions)
{
    if (pyramid.baseBucket() != 1 || bucket <= 1 || values.size() < 2 ||
        values.size() != positions.size() || values.size() % 2 != 0) {
        return;
    }
    const double* raw = pyramid.levels.front().values.data();
    const long long rawCount = pyramid.length;
    const long long limit = std::min(first + count, rawCount);
    const long long buckets = static_cast<long long>(values.size() / 2);
    std::vector<double> seamed;
    std::vector<double> placed;
    seamed.reserve(values.size() * 2);
    placed.reserve(values.size() * 2);
    for (long long i = 0; i < buckets; ++i) {
        const long long start = first + i * bucket;
        if (start >= limit) {
            break;
        }
        const long long end = std::min(start + bucket, limit) - 1;
        const long long next = end + 1;
        // The following bucket opens on the next sample. Its entry is this
        // exit's neighbour, so one of the two is the seam.
        const bool followed = i + 1 < buckets && next < limit && next < rawCount &&
                              std::isfinite(raw[next]);
        const auto at = static_cast<std::size_t>(i) * 2;
        appendColumn(seamed, placed, raw, rawCount, start, end, values[at], positions[at],
                     values[at + 1], positions[at + 1], !followed);
    }
    values.swap(seamed);
    positions.swap(placed);
}

/// `positions`, when set, is filled alongside an envelope and cleared when the
/// answer is the samples themselves. See fillWindow.
[[nodiscard]] bool foldRun(const LinePyramid& pyramid, long long first, long long span,
                           long long bucket, std::vector<double>& out,
                           std::vector<double>* positions)
{
    out.clear();
    if (positions != nullptr) {
        positions->clear();
    }
    if (pyramid.empty() || bucket < pyramid.baseBucket() || first < 0 || span <= 0) {
        return false;
    }
    const int at = levelFor(pyramid, bucket);
    if (at < 0) {
        return false;
    }
    const PyramidLevel& level = pyramid.levels[static_cast<std::size_t>(at)];
    if (first % level.bucket != 0) {
        // Nothing in the plot asks for one -- windowFor aligns a run to a
        // multiple of its own bucket, and every level's bucket divides that --
        // but an unaligned fold would silently answer about the wrong elements,
        // so it is refused rather than approximated.
        return false;
    }
    const long long take = std::min(span, pyramid.length - first);
    if (take <= 0) {
        return false;
    }
    const long long factor = bucket / level.bucket;

    if (level.bucket == 1) {
        if (bucket == 1) {
            // Sample for sample. The consecutive path of a read, which answers
            // with the elements themselves rather than with pairs of them.
            out.assign(level.values.data() + first, level.values.data() + first + take);
            return true;
        }
        // Sized and then written in ranges rather than appended to, so that the
        // walk can be spread over the machine. It matters in one place and
        // matters a lot there: the whole-line summary's stride is whatever
        // `ceil(length / buckets)` comes to and is usually odd, so the only
        // level that divides it is the base -- which makes the summary a fold
        // of every element of the line. On ten million that was a third of what
        // the first picture cost. Every *other* fold is a paneful of buckets and
        // stays on this thread, because below the grain a thread pool costs more
        // than the fold does.
        const long long made = (take + bucket - 1) / bucket;
        out.resize(static_cast<std::size_t>(made) * 2);
        double* into = out.data();
        const double* from = level.values.data() + first;
        if (positions == nullptr) {
            overRanges(made, take, [&](long long a, long long b) {
                const long long at = a * bucket;
                reduceBucketsInto(from + at, std::min(b * bucket, take) - at, bucket,
                                  into + a * 2);
            });
            return true;
        }
        // The index extremesOf finds while it folds. One pass: asking again
        // afterwards would walk every element of the line a second time, and
        // the whole-line summary is that walk.
        positions->resize(static_cast<std::size_t>(made) * 2);
        double* atPos = positions->data();
        overRanges(made, take, [&](long long a, long long b) {
            for (long long bucketIndex = a; bucketIndex < b; ++bucketIndex) {
                const long long at = bucketIndex * bucket;
                const long long count = std::min(at + bucket, take) - at;
                const Extremes found = extremesOf(from + at, 0, count);
                const auto outAt = static_cast<std::size_t>(bucketIndex) * 2;
                if (!found.found()) {
                    const auto nothing = std::numeric_limits<double>::quiet_NaN();
                    into[outAt] = nothing;
                    into[outAt + 1] = nothing;
                    atPos[outAt] = static_cast<double>(first + at);
                    atPos[outAt + 1] = static_cast<double>(first + at);
                    continue;
                }
                into[outAt] = found.first();
                into[outAt + 1] = found.second();
                atPos[outAt] = static_cast<double>(first + at + found.firstAt());
                atPos[outAt + 1] = static_cast<double>(first + at + found.secondAt());
            }
        });
        withSeams(pyramid, bucket, first, take, out, *positions);
        return true;
    }

    const long long fromBucket = first / level.bucket;
    const long long buckets =
        std::min((take + level.bucket - 1) / level.bucket, level.buckets() - fromBucket);
    if (buckets <= 0) {
        return false;
    }
    const double* pairs = level.values.data() + fromBucket * 2;
    const long long made = (buckets + factor - 1) / factor;
    if (factor == 1) {
        out.assign(pairs, pairs + buckets * 2);
    }
    else {
        out.resize(static_cast<std::size_t>(made) * 2);
        double* into = out.data();
        overRanges(made, buckets * 2, [&](long long a, long long b) {
            const long long start = a * factor;
            coarsenEnvelopeInto(pairs + start * 2, std::min(b * factor, buckets) - start, factor,
                                into + a * 2);
        });
    }
    if (positions == nullptr) {
        return true;
    }
    // The value was copied up from a child. Finding which child, and then
    // which of its children, is a handful of comparisons a level -- never a
    // scan of the bucket, which would make the summary a second pass over
    // the line.
    positions->resize(out.size());
    for (long long i = 0; i < made; ++i) {
        const long long n = std::min(factor, buckets - i * factor);
        const long long src = fromBucket + i * factor;
        // src counts buckets of this level from the start of the line, so
        // the element it opens on is src times the level's own bucket.
        const long long bucketOrigin = src * level.bucket;
        const double earlier = out[static_cast<std::size_t>(i) * 2];
        const double later = out[static_cast<std::size_t>(i) * 2 + 1];
        (*positions)[static_cast<std::size_t>(i) * 2] = static_cast<double>(
            positionOf(pyramid, at, src, n, earlier, bucketOrigin));
        const long long laterFallback =
            std::isfinite(earlier) && earlier == later
                ? bucketOrigin
                : std::min(bucketOrigin + bucket / 2, std::max<long long>(pyramid.length - 1, 0));
        (*positions)[static_cast<std::size_t>(i) * 2 + 1] =
            static_cast<double>(positionOf(pyramid, at, src, n, later, laterFallback));
    }
    withSeams(pyramid, bucket, first, take, out, *positions);
    return true;
}

struct Bend
{
    long long index = -1;
    double value = 0.0;
    double error = 0.0;
};

/// The extreme of a finer level furthest off the line from `(i0, y0)` to `(i1, y1)`.
///
/// A handful of buckets, not the samples between the two ends. The point
/// farthest from a straight line is an extreme of one of those buckets, and
/// `locateSlot` names the sample. Eight buckets is the most one call looks at:
/// coarser than that and the next call, on the piece that was actually bent,
/// opens the level below.
[[nodiscard]] Bend farthestBend(const LinePyramid& pyramid, double i0, double y0, double i1,
                                double y1)
{
    Bend best;
    const double span = i1 - i0;
    if (!(span > 1.0) || !std::isfinite(y0) || !std::isfinite(y1)) {
        return best;
    }
    int chosen = -1;
    for (std::size_t i = 0; i < pyramid.levels.size(); ++i) {
        const long long bucket = pyramid.levels[i].bucket;
        if (bucket >= span) {
            break;
        }
        // Finest level that still covers the gap in eight buckets or fewer.
        // Finer than that is a scan, which is what this exists not to be.
        if (span / static_cast<double>(bucket) > 8.0) {
            continue;
        }
        chosen = static_cast<int>(i);
        break;
    }
    if (chosen < 0) {
        return best;
    }
    const PyramidLevel& level = pyramid.levels[static_cast<std::size_t>(chosen)];
    const auto offer = [&](long long index, double value) {
        if (index <= i0 || index >= i1 || !std::isfinite(value)) {
            return;
        }
        const double on = y0 + (static_cast<double>(index) - i0) / span * (y1 - y0);
        const double error = std::abs(value - on);
        if (error > best.error) {
            best.error = error;
            best.index = index;
            best.value = value;
        }
    };
    if (level.bucket == 1) {
        const auto from = static_cast<long long>(std::floor(i0)) + 1;
        const auto to = std::min(static_cast<long long>(std::ceil(i1)), pyramid.length);
        for (long long k = std::max<long long>(from, 0); k < to; ++k) {
            offer(k, level.values[static_cast<std::size_t>(k)]);
        }
        return best;
    }
    const long long firstBucket = std::max<long long>(static_cast<long long>(std::floor(i0)) / level.bucket, 0);
    const long long lastBucket = std::min((static_cast<long long>(std::ceil(i1)) + level.bucket - 1) / level.bucket,
                                          level.buckets());
    for (long long b = firstBucket; b < lastBucket; ++b) {
        const double* pair = level.values.data() + static_cast<std::size_t>(b) * 2;
        offer(locateSlot(pyramid, chosen, b, 0), pair[0]);
        offer(locateSlot(pyramid, chosen, b, 1), pair[1]);
    }
    return best;
}

void bendBetween(const LinePyramid& pyramid, double i0, double y0, double i1, double y1,
                 double yTolerance, int& budget, int depth, std::vector<double>& outValues,
                 std::vector<double>& outPositions)
{
    // Depth bounds the chain that peels one sample at a time. A sine splits
    // near the middle and never gets here; noise would, and the stack is not
    // the place to hold a line.
    if (budget <= 0 || depth > 32 || !(i1 > i0 + 1.0)) {
        return;
    }
    const Bend worst = farthestBend(pyramid, i0, y0, i1, y1);
    if (worst.index < 0 || !(worst.error > yTolerance)) {
        return;
    }
    --budget;
    bendBetween(pyramid, i0, y0, static_cast<double>(worst.index), worst.value, yTolerance, budget,
                depth + 1, outValues, outPositions);
    outValues.push_back(worst.value);
    outPositions.push_back(static_cast<double>(worst.index));
    bendBetween(pyramid, static_cast<double>(worst.index), worst.value, i1, y1, yTolerance, budget,
                depth + 1, outValues, outPositions);
}

} // namespace

bool fillWindow(const LinePyramid& pyramid, const PlotWindow& window, std::vector<double>& out,
                std::vector<double>* positions)
{
    return foldRun(pyramid, window.first, window.span, window.bucket, out, positions);
}

void followCurve(const LinePyramid& pyramid, std::vector<double>& values,
                 std::vector<double>& positions, double yTolerance, int budget)
{
    if (!(yTolerance > 0.0) || budget <= 0 || pyramid.empty() || values.size() < 2 ||
        values.size() != positions.size()) {
        return;
    }
    std::vector<double> outValues;
    std::vector<double> outPositions;
    outValues.reserve(values.size() + static_cast<std::size_t>(budget));
    outPositions.reserve(outValues.capacity());
    outValues.push_back(values.front());
    outPositions.push_back(positions.front());
    for (std::size_t i = 1; i < values.size(); ++i) {
        const double i0 = outPositions.back();
        const double y0 = outValues.back();
        const double i1 = positions[i];
        const double y1 = values[i];
        if (budget > 0 && std::isfinite(y0) && std::isfinite(y1) && i1 > i0 + 1.0) {
            bendBetween(pyramid, i0, y0, i1, y1, yTolerance, budget, 0, outValues, outPositions);
        }
        outValues.push_back(y1);
        outPositions.push_back(i1);
    }
    values.swap(outValues);
    positions.swap(outPositions);
}

bool fillWhole(const LinePyramid& pyramid, int buckets, std::vector<double>& out, long long& stride,
               double& step, std::vector<double>* positions)
{
    out.clear();
    if (positions != nullptr) {
        positions->clear();
    }
    if (pyramid.empty() || buckets <= 0) {
        return false;
    }
    const long long base = pyramid.baseBucket();
    long long wanted = (pyramid.length + buckets - 1) / buckets;
    // Up to a multiple of the base, which is what makes the fold exact. With a
    // base of one -- every line the budget can hold raw, which is every line in
    // the test suite and most of the ones a reader opens -- this changes nothing
    // at all and the answer is the file's own, element for element.
    //
    // It is not rounded any further than that. A coarser stride would fold out
    // of a coarser level and cost a fraction of the walk, but the summary is
    // what the y axis is drawn against and what the footer counts, and buying a
    // few milliseconds with a resolution nobody asked to give up is the wrong
    // trade. The walk is spread over the machine instead -- see foldRun.
    wanted = ((wanted + base - 1) / base) * base;
    stride = std::max<long long>(wanted, 1);

    if (!foldRun(pyramid, 0, pyramid.length, stride, out, positions)) {
        return false;
    }
    // What sampleFrom reports beside the values: an envelope answers with two
    // per bucket half a bucket apart, and a stride of one is the elements
    // themselves.
    step = stride == 1 ? 1.0 : static_cast<double>(stride) / 2.0;
    return true;
}

Extremes extremesOver(const LinePyramid& pyramid, long long first, long long last)
{
    Extremes found;
    if (pyramid.empty()) {
        return found;
    }
    const long long base = pyramid.baseBucket();
    first = std::max<long long>(first, 0);
    last = std::min(last, pyramid.length);
    first = (first / base) * base;
    if (last < pyramid.length) {
        last = ((last + base - 1) / base) * base;
        last = std::min(last, pyramid.length);
    }

    // Which bucket the winning value was copied out of. The index stored on
    // the way is only there so a value that never resolves still has a place;
    // the sample is walked down once, after the winners are known, because
    // doing it for every bucket the run is cut into would open levels the
    // answer does not use.
    struct Hit
    {
        int level = -1;
        long long bucket = -1;
        int slot = 0;
        long long at = -1;
    };
    Hit lowHit;
    Hit highHit;
    const auto take = [&](double value, const Hit& hit) {
        if (!std::isfinite(value)) {
            return;
        }
        if (found.lowAt < 0 || value < found.lowest) {
            found.lowest = value;
            found.lowAt = hit.at;
            lowHit = hit;
        }
        if (found.highAt < 0 || value > found.highest) {
            found.highest = value;
            found.highAt = hit.at;
            highHit = hit;
        }
    };

    long long at = first;
    while (at < last) {
        // The coarsest level whose bucket starts here and ends inside the run
        // -- or at the end of the line, which a run reaching it is allowed to
        // take whole. The base always qualifies, because `at` is on it.
        std::size_t use = 0;
        for (std::size_t i = 1; i < pyramid.levels.size(); ++i) {
            const long long bucket = pyramid.levels[i].bucket;
            if (at % bucket != 0 || std::min(at + bucket, pyramid.length) > last) {
                break; // coarser still cannot fit where this one did not
            }
            use = i;
        }
        const PyramidLevel& level = pyramid.levels[use];
        const long long index = at / level.bucket;
        if (level.bucket == 1) {
            take(level.values[static_cast<std::size_t>(index)],
                 Hit{-1, -1, 0, at});
        }
        else if (index < level.buckets()) {
            // A pair in the order its two occurred. The index kept here is the
            // bucket's start and its middle; locateSlot replaces it with the
            // sample once this value has won.
            const double* pair = level.values.data() + static_cast<std::size_t>(index) * 2;
            take(pair[0], Hit{static_cast<int>(use), index, 0, at});
            take(pair[1], Hit{static_cast<int>(use), index, 1, at + level.bucket / 2});
        }
        at += level.bucket;
    }
    const auto refine = [&](const Hit& hit) {
        if (hit.level < 0) {
            return hit.at;
        }
        return locateSlot(pyramid, hit.level, hit.bucket, hit.slot);
    };
    if (lowHit.at >= 0) {
        found.lowAt = refine(lowHit);
    }
    if (highHit.at >= 0) {
        found.highAt = refine(highHit);
    }
    return found;
}

void foldColumns(const LinePyramid& pyramid, std::span<const double> edges, ColumnFold& out)
{
    out.values.clear();
    out.positions.clear();
    out.summarised = false;
    if (pyramid.empty() || edges.size() < 2) {
        return;
    }
    const long long base = pyramid.baseBucket();
    const long long length = pyramid.length;
    const PyramidLevel& bottom = pyramid.levels.front();
    out.values.reserve(edges.size() * 2);
    out.positions.reserve(edges.size() * 2);

    // Where the last column stopped. A column never starts before it, which is
    // what keeps an element out of two columns once the ends are rounded to
    // the base.
    long long cursor = 0;
    for (std::size_t c = 0; c + 1 < edges.size(); ++c) {
        const double from = edges[c];
        const double to = edges[c + 1];
        if (!(to > from) || !(to > 0.0) || !(from < static_cast<double>(length))) {
            continue;
        }
        long long first = static_cast<long long>(std::ceil(std::max(from, 0.0)));
        long long last =
            static_cast<long long>(std::min(std::ceil(to), static_cast<double>(length)));
        if (base > 1) {
            first = (first / base) * base;
            last = std::min(((last + base - 1) / base) * base, length);
        }
        first = std::max(first, cursor);
        if (last <= first) {
            continue;
        }
        cursor = last;

        if (base == 1 && last - first <= 2) {
            for (long long i = first; i < last; ++i) {
                out.values.push_back(bottom.values[static_cast<std::size_t>(i)]);
                out.positions.push_back(static_cast<double>(i));
            }
            continue;
        }

        out.summarised = true;
        const Extremes found = extremesOver(pyramid, first, last);
        if (!found.found()) {
            out.values.push_back(std::numeric_limits<double>::quiet_NaN());
            out.positions.push_back(static_cast<double>(first));
            continue;
        }
        // The same column the linear fold draws: the seams as well as the
        // extremes, and only while the base still has the samples to name
        // them. Above that the pair is all the level kept.
        if (base == 1) {
            // The next column that is actually drawn. It opens on `last` when
            // the edges abut, and that sample is the neighbour of this exit.
            bool followed = false;
            if (last < length &&
                std::isfinite(bottom.values[static_cast<std::size_t>(last)])) {
                long long probe = last;
                for (std::size_t n = c + 1; n + 1 < edges.size(); ++n) {
                    const double nFrom = edges[n];
                    const double nTo = edges[n + 1];
                    if (!(nTo > nFrom) || !(nTo > 0.0) ||
                        !(nFrom < static_cast<double>(length))) {
                        continue;
                    }
                    long long nFirst =
                        static_cast<long long>(std::ceil(std::max(nFrom, 0.0)));
                    const long long nLast = static_cast<long long>(
                        std::min(std::ceil(nTo), static_cast<double>(length)));
                    nFirst = std::max(nFirst, probe);
                    if (nLast <= nFirst) {
                        continue;
                    }
                    followed = nFirst == last;
                    break;
                }
            }
            appendColumn(out.values, out.positions, bottom.values.data(), length, first,
                         last - 1, found.first(), static_cast<double>(found.firstAt()),
                         found.second(), static_cast<double>(found.secondAt()), !followed);
        }
        else {
            out.values.push_back(found.first());
            out.positions.push_back(static_cast<double>(found.firstAt()));
            out.values.push_back(found.second());
            out.positions.push_back(static_cast<double>(found.secondAt()));
        }
    }
}

bool smallestPositive(const LinePyramid& pyramid, double& out)
{
    if (pyramid.empty()) {
        return false;
    }
    constexpr long long factor = 1LL << kPyramidOctaves;
    bool found = false;
    double best = 0.0;
    const auto offer = [&](double value) {
        if ((!found || value < best) && value > 0.0 && std::isfinite(value)) {
            best = value;
            found = true;
        }
    };

    // Depth first, with the buckets still to open on a stack of (level,
    // bucket). The top level is a handful of buckets, so it seeds the stack.
    std::vector<std::pair<std::size_t, long long>> open;
    const std::size_t top = pyramid.levels.size() - 1;
    for (long long j = pyramid.levels[top].buckets() - 1; j >= 0; --j) {
        open.emplace_back(top, j);
    }
    while (!open.empty()) {
        const auto [at, j] = open.back();
        open.pop_back();
        const PyramidLevel& level = pyramid.levels[at];
        if (level.bucket == 1) {
            offer(level.values[static_cast<std::size_t>(j)]);
            continue;
        }
        const double a = level.values[static_cast<std::size_t>(j) * 2];
        const double b = level.values[static_cast<std::size_t>(j) * 2 + 1];
        if (std::isnan(a) || std::isnan(b)) {
            continue; // nothing finite under it
        }
        const double low = std::min(a, b);
        const double high = std::max(a, b);
        if (!(high > 0.0)) {
            continue; // nothing above zero under it
        }
        if (low > 0.0) {
            // All of it is above zero, so its smallest is its answer.
            offer(low);
            continue;
        }
        if (at == 0) {
            // A bucket of the base that straddles zero: its largest is the
            // nearest thing above zero it can vouch for.
            offer(high);
            continue;
        }
        const PyramidLevel& below = pyramid.levels[at - 1];
        const long long last = std::min((j + 1) * factor, below.buckets());
        for (long long child = last - 1; child >= j * factor; --child) {
            open.emplace_back(at - 1, child);
        }
    }
    out = best;
    return found;
}

} // namespace gui
