#if !defined(HYPERLOGLOG_HPP)
#define HYPERLOGLOG_HPP

/**
 * @file hyperloglog.hpp
 * @brief HyperLogLog cardinality estimator
 * @date Created 2013/3/20
 * @author Hideaki Ohno
 *
 * Vendored from https://github.com/hideo55/cpp-HyperLogLog
 *   upstream commit: 517598b2fe3149291b007626f65191b95f750108
 *   license: MIT (per upstream package.json)
 *
 * Local patches applied on top of upstream:
 *   - hideo55/cpp-HyperLogLog#15: replace `M_[r] |= other.M_[r]` with
 *     `M_[r] = other.M_[r]` in HyperLogLog::merge and
 *     HyperLogLogHIP::merge.  Merge must take the per-register max,
 *     not bitwise-OR.  Original inflated registers (e.g. 5|6 = 7
 *     instead of 6) and overestimated cardinality after a merge.
 *   - Replaced bundled 32-bit MurmurHash3 with the already-vendored
 *     xxHash (XXH3_64bits_withSeed).  Lifts the hash space from 2^32
 *     to 2^64, so the cardinality ceiling moves from ~2^(32-b) to
 *     ~2^(64-b) and the Flajolet-style large-range correction (which
 *     was specifically for 32-bit hash collisions) is no longer
 *     needed and has been removed.
 *   - Dropped C++17-removed dynamic exception specifications
 *     (`throw (std::invalid_argument)`, `throw (std::runtime_error)`).
 */

#include <vector>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cstdint>

#include "xxHash/xxhash.h"

#define HLL_HASH_SEED 313

namespace hll {

// Returns clz(x) + 1, capped at (b + 1).  When x is zero,
// __builtin_clzll is undefined behavior, so short-circuit to the
// max rank.  In practice x == 0 only when the lower (64 - b) bits
// of the hash are all zero — probability 2^-(64-b), e.g. 2^-52 at
// b=12 — but worth handling deterministically.
inline uint8_t leading_zero_rank(uint64_t x, uint8_t b) {
    if (x == 0) {
        return static_cast<uint8_t>(b + 1);
    }
#if defined(__GNUC__) || defined(__clang__)
    uint8_t clz = static_cast<uint8_t>(__builtin_clzll(x));
#elif defined(_MSC_VER)
    unsigned long pos = 0;
    _BitScanReverse64(&pos, x);
    uint8_t clz = static_cast<uint8_t>(63 - pos);
#else
    uint8_t clz = 0;
    while (clz < 64 && !(x & (1ULL << 63))) {
        clz += 1;
        x <<= 1;
    }
#endif
    return static_cast<uint8_t>(std::min<uint8_t>(b, clz) + 1);
}

/** @class HyperLogLog
 *  @brief Implement of 'HyperLogLog' estimate cardinality algorithm
 */
class HyperLogLog {
public:

    /**
     * Constructor
     *
     * @param[in] b bit width (register size will be 2 to the b power).
     *            This value must be in the range[4,30].Default value is 4.
     *
     * @exception std::invalid_argument the argument is out of range.
     */
    HyperLogLog(uint8_t b = 4) :
            b_(b), m_(1 << b), M_(m_, 0) {

        if (b < 4 || 30 < b) {
            throw std::invalid_argument("bit width must be in the range [4,30]");
        }

        double alpha;
        switch (m_) {
            case 16:
                alpha = 0.673;
                break;
            case 32:
                alpha = 0.697;
                break;
            case 64:
                alpha = 0.709;
                break;
            default:
                alpha = 0.7213 / (1.0 + 1.079 / m_);
                break;
        }
        alphaMM_ = alpha * m_ * m_;
    }

    /**
     * Adds element to the estimator
     *
     * @param[in] str string to add
     * @param[in] len length of string
     */
    void add(const char* str, uint32_t len) {
        uint64_t hash = XXH3_64bits_withSeed(str, len, HLL_HASH_SEED);
        uint64_t index = hash >> (64 - b_);
        uint8_t rank = leading_zero_rank(hash << b_,
                                         static_cast<uint8_t>(64 - b_));
        if (rank > M_[index]) {
            M_[index] = rank;
        }
    }

    /**
     * Estimates cardinality value.
     *
     * @return Estimated cardinality value.
     */
    double estimate() const {
        double estimate;
        double sum = 0.0;
        for (uint32_t i = 0; i < m_; i++) {
            sum += 1.0 / (uint64_t(1) << M_[i]);
        }
        estimate = alphaMM_ / sum; // E in the original paper
        if (estimate <= 2.5 * m_) {
            uint32_t zeros = 0;
            for (uint32_t i = 0; i < m_; i++) {
                if (M_[i] == 0) {
                    zeros++;
                }
            }
            if (zeros != 0) {
                estimate = m_ * std::log(static_cast<double>(m_)/ zeros);
            }
        }
        // The Flajolet large-range correction (estimate > 2^32 / 30)
        // existed to compensate for hash-space saturation with the
        // bundled 32-bit MurmurHash3.  With XXH64, the hash space is
        // 2^64 and saturation is unreachable in practice, so the
        // correction is dropped.
        return estimate;
    }

    /**
     * Merges the estimate from 'other' into this object, returning the estimate of their union.
     * The number of registers in each must be the same.
     *
     * @param[in] other HyperLogLog instance to be merged
     *
     * @exception std::invalid_argument number of registers doesn't match.
     */
    void merge(const HyperLogLog& other) {
        if (m_ != other.m_) {
            std::stringstream ss;
            ss << "number of registers doesn't match: " << m_ << " != " << other.m_;
            throw std::invalid_argument(ss.str().c_str());
        }
        for (uint32_t r = 0; r < m_; ++r) {
            if (M_[r] < other.M_[r]) {
                // Per-register max — see PR #15 in the file header.
                M_[r] = other.M_[r];
            }
        }
    }

    /**
     * Clears all internal registers.
     */
    void clear() {
        std::fill(M_.begin(), M_.end(), 0);
    }

    /**
     * Returns size of register.
     *
     * @return Register size
     */
    uint32_t registerSize() const {
        return m_;
    }

    /**
     * Exchanges the content of the instance
     *
     * @param[in,out] rhs Another HyperLogLog instance
     */
    void swap(HyperLogLog& rhs) {
        std::swap(b_, rhs.b_);
        std::swap(m_, rhs.m_);
        std::swap(alphaMM_, rhs.alphaMM_);
        M_.swap(rhs.M_);
    }

    /**
     * Dump the current status to a stream
     *
     * @param[out] os The output stream where the data is saved
     *
     * @exception std::runtime_error When failed to dump.
     */
    void dump(std::ostream& os) const {
        os.write((char*)&b_, sizeof(b_));
        os.write((char*)&M_[0], sizeof(M_[0]) * M_.size());
        if(os.fail()){
            throw std::runtime_error("Failed to dump");
        }
    }

    /**
     * Restore the status from a stream
     *
     * @param[in] is The input stream where the status is saved
     *
     * @exception std::runtime_error When failed to restore.
     */
    void restore(std::istream& is) {
        uint8_t b = 0;
        is.read((char*)&b, sizeof(b));
        HyperLogLog tempHLL(b);
        is.read((char*)&(tempHLL.M_[0]), sizeof(M_[0]) * tempHLL.m_);
        if(is.fail()){
           throw std::runtime_error("Failed to restore");
        }
        swap(tempHLL);
    }

protected:
    uint8_t b_; ///< register bit width
    uint32_t m_; ///< register size
    double alphaMM_; ///< alpha * m^2
    std::vector<uint8_t> M_; ///< registers
};

/**
 * @brief HIP estimator on HyperLogLog counter.
 */
class HyperLogLogHIP : public HyperLogLog {
public:

    /**
     * Constructor
     *
     * @param[in] b bit width (register size will be 2 to the b power).
     *            This value must be in the range[4,30].Default value is 4.
     *
     * @exception std::invalid_argument the argument is out of range.
     */
    HyperLogLogHIP(uint8_t b = 4) : HyperLogLog(b), register_limit_(63), c_(0.0), p_(1 << b) {
    }

    /**
     * Adds element to the estimator
     *
     * @param[in] str string to add
     * @param[in] len length of string
     */
    void add(const char* str, uint32_t len) {
        uint64_t hash = XXH3_64bits_withSeed(str, len, HLL_HASH_SEED);
        uint64_t index = hash >> (64 - b_);
        uint8_t rank = leading_zero_rank(hash << b_,
                                         static_cast<uint8_t>(64 - b_));
        rank = rank == 0 ? register_limit_ : std::min(register_limit_, rank);
        const uint8_t old = M_[index];
        if (rank > old) {
            c_ += 1.0 / (p_/m_);
            p_ -= 1.0/(uint64_t(1) << old);
            M_[index] = rank;
            if(rank < register_limit_){
                p_ += 1.0/(uint64_t(1) << rank);
            }
        }
    }

    /**
     * Estimates cardinality value.
     *
     * @return Estimated cardinality value.
     */
    double estimate() const {
        return c_;
    }

    /**
     * Merges the estimate from 'other' into this object, returning the estimate of their union.
     * The number of registers in each must be the same.
     *
     * @param[in] other HyperLogLog instance to be merged
     *
     * @exception std::invalid_argument number of registers doesn't match.
     */
    void merge(const HyperLogLogHIP& other) {
        if (m_ != other.m_) {
            std::stringstream ss;
            ss << "number of registers doesn't match: " << m_ << " != " << other.m_;
            throw std::invalid_argument(ss.str().c_str());
        }
        for (uint32_t r = 0; r < m_; ++r) {
            const uint8_t b = M_[r];
            const uint8_t b_other = other.M_[r];
            if (b < b_other) {
                c_ += 1.0 / (p_/m_);
                p_ -= 1.0/(uint64_t(1) << b);
                // Per-register max — see PR #15 in the file header.
                M_[r] = b_other;
                if(b_other < register_limit_){
                    p_ += 1.0/(uint64_t(1) << b_other);
                }
            }
        }
    }

    /**
     * Clears all internal registers.
     */
    void clear() {
        std::fill(M_.begin(), M_.end(), 0);
        c_ = 0.0;
        p_ = 1 << b_;
    }

    /**
     * Returns size of register.
     *
     * @return Register size
     */
    uint32_t registerSize() const {
        return m_;
    }

    /**
     * Exchanges the content of the instance
     *
     * @param[in,out] rhs Another HyperLogLog instance
     */
    void swap(HyperLogLogHIP& rhs) {
        std::swap(b_, rhs.b_);
        std::swap(m_, rhs.m_);
        std::swap(c_, rhs.c_);
        M_.swap(rhs.M_);
    }

    /**
     * Dump the current status to a stream
     *
     * @param[out] os The output stream where the data is saved
     *
     * @exception std::runtime_error When failed to dump.
     */
    void dump(std::ostream& os) const {
        os.write((char*)&b_, sizeof(b_));
        os.write((char*)&M_[0], sizeof(M_[0]) * M_.size());
        os.write((char*)&c_, sizeof(c_));
        os.write((char*)&p_, sizeof(p_));
        if(os.fail()){
            throw std::runtime_error("Failed to dump");
        }
    }

    /**
     * Restore the status from a stream
     *
     * @param[in] is The input stream where the status is saved
     *
     * @exception std::runtime_error When failed to restore.
     */
    void restore(std::istream& is) {
        uint8_t b = 0;
        is.read((char*)&b, sizeof(b));
        HyperLogLogHIP tempHLL(b);
        is.read((char*)&(tempHLL.M_[0]), sizeof(M_[0]) * tempHLL.m_);
        is.read((char*)&(tempHLL.c_), sizeof(double));
        is.read((char*)&(tempHLL.p_), sizeof(double));
        if(is.fail()){
           throw std::runtime_error("Failed to restore");
        }
        swap(tempHLL);
    }
private:
    const uint8_t register_limit_;
    double c_;
    double p_;
};

} // namespace hll

#endif // !defined(HYPERLOGLOG_HPP)
