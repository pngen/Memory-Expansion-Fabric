#pragma once
// Memory Expansion Fabric - error and Result types.
#include <string>
#include <utility>
#include <optional>

namespace memory_expansion_fabric {

// Generic operation error codes. Domain-specific typed reasons (eligibility)
// are separate and returned alongside, not collapsed into these.
enum class ErrorCode : std::uint8_t {
    OK,
    INVALID_ID,
    DUPLICATE_ID,
    NOT_FOUND,
    STALE_AUTHORITY,
    STALE_EVIDENCE,
    REVALIDATION_REQUIRED,
    WRONG_GENERATION,
    WRONG_EPOCH,
    INVALID_STATE_TRANSITION,
    INVALID_ARGUMENT,
    CAPACITY_OVERFLOW,
    CAPACITY_UNDERFLOW,
    CAPACITY_INSUFFICIENT,
    POOL_OVERCOMMIT,
    PROVIDER_RETIRED,
    REGION_RETIRED,
    POLICY_DENIED,
    ALREADY_EXISTS,
    NOT_SUPPORTED,
    UNKNOWN,
    INTERNAL,
    CORRUPT,
    IO_ERROR,
    PROTOCOL_ERROR,
    FRAME_TOO_LARGE,
    FRAME_TRUNCATED,
    FRAME_BAD_TYPE,
    FRAME_CHECKSUM,
    DUPLICATE_CAPACITY,
    CAPACITY_RESURRECTION
};

struct Error {
    ErrorCode code = ErrorCode::OK;
    std::string message;
    explicit operator bool() const { return code != ErrorCode::OK; }
};

inline constexpr ErrorCode OkCode() { return ErrorCode::OK; }

template <class T>
class Result {
public:
    Result() : ok_(false), error_{ErrorCode::INTERNAL, "default-constructed Result"} {}

    static Result ok(T value) {
        Result r;
        r.ok_ = true;
        r.value_ = std::move(value);
        return r;
    }
    static Result fail(ErrorCode code, std::string message) {
        Result r;
        r.ok_ = false;
        r.error_ = Error{code, std::move(message)};
        return r;
    }
    static Result fail(Error err) {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(err);
        return r;
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    // These deliberately abort on misuse rather than silently yielding a
    // default: an unhandled error must be visible, not papered over.
    const T& value() const { return value_; }
    T& value() { return value_; }
    T moveValue() { return std::move(value_); }

    const Error& error() const { return error_; }

private:
    bool ok_ = false;
    T value_{};
    Error error_{};
};

// Specialization for void-returning operations.
template <>
class Result<void> {
public:
    Result() : ok_(true) {}
    static Result success() { return Result(); }
    static Result fail(ErrorCode code, std::string message) {
        Result r;
        r.ok_ = false;
        r.error_ = Error{code, std::move(message)};
        return r;
    }
    static Result fail(Error err) {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(err);
        return r;
    }
    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }
private:
    bool ok_ = true;
    Error error_{};
};

} // namespace memory_expansion_fabric
