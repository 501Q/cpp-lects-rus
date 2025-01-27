//------------------------------------------------------------------------------
//
// Header for generators
//
// generator -- simplest generator, program-resumable
// example: natseq.cc
//
// range_gen -- more sophisticated generator for range-based iteration
// example: natseq_range.cc
//
// rec_generator
//
//------------------------------------------------------------------------------
//
// This file is licensed after LGPL v3
// Look at: https://www.gnu.org/licenses/lgpl-3.0.en.html for details
//
//------------------------------------------------------------------------------

#pragma once

#include "coroinclude.hpp"

//------------------------------------------------------------------------------
//
// generator
//
//------------------------------------------------------------------------------

template <typename T> struct generator {
  struct promise_type {
    T current_value;
    using coro_handle = coro::coroutine_handle<promise_type>;
    auto get_return_object() { return coro_handle::from_promise(*this); }
    auto initial_suspend() { return coro::suspend_always(); }
    auto final_suspend() noexcept { return coro::suspend_always(); }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
    auto yield_value(T value) {
      current_value = value;
      return coro::suspend_always{};
    }
  };

  using coro_handle = coro::coroutine_handle<promise_type>;
  bool move_next() {
    return handle_ ? (handle_.resume(), !handle_.done()) : false;
  }
  T current_value() const { return handle_.promise().current_value; }
  generator(coro_handle h) : handle_(h) {}
  generator(generator const &) = delete;
  generator(generator &&rhs) : handle_(rhs.handle_) { rhs.handle_ = nullptr; }
  ~generator() {
    if (handle_)
      handle_.destroy();
  }

private:
  coro_handle handle_;
};

//------------------------------------------------------------------------------
//
// range_gen
//
//------------------------------------------------------------------------------

template <typename T> struct range_gen {
  struct promise_type {
    const T *value = nullptr;
#ifdef BUG
    auto get_return_object() { return *this; }
#else
    auto get_return_object() { return coro_handle::from_promise(*this); }
#endif
    coro::suspend_always initial_suspend() { return {}; }
    coro::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
    coro::suspend_always yield_value(const T &val) {
      value = std::addressof(val);
      return {};
    }
  };

  using coro_handle = coro::coroutine_handle<promise_type>;

  struct iterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = const T *;
    using reference = const T &;

    coro_handle handle;
    iterator(coro_handle h) : handle(h) {}

    iterator &operator++() {
      handle.resume();
      if (handle.done())
        handle = nullptr;
      return *this;
    }

    T const &operator*() const { return *handle.promise().value; }
    T const *operator->() const { return handle.promise().value; }
    auto operator<=>(const iterator &rhs) const noexcept = default;
  };

  range_gen(promise_type &promise)
      : handle_(coro_handle::from_promise(promise)) {}
  range_gen(coro_handle h) : handle_(h) {}
  range_gen(const range_gen &) = delete;
  range_gen &operator=(const range_gen &other) = delete;
  range_gen(range_gen &&rhs) : handle_(rhs.handle_) {
    std::cout << "range_gen(range_gen&& rhs)\n";
    rhs.handle_ = nullptr;
  }
  range_gen &operator=(range_gen &&rhs) {
    std::cout << "range_gen &operator=(range_gen&& rhs)\n";
    if (this != &rhs) {
      handle_ = rhs.handle;
      rhs.handle = nullptr;
    }
    return *this;
  }

  ~range_gen() {
    if (handle_)
      handle_.destroy();
  }

  iterator begin() {
    if (!handle_)
      return iterator{nullptr};
    handle_.resume();
    if (handle_.done())
      return iterator{nullptr};
    return handle_;
  }

  iterator end() { return iterator{nullptr}; }

private:
  coro_handle handle_;
};

//------------------------------------------------------------------------------
//
// rec_generator
//
//------------------------------------------------------------------------------

template <typename T> struct rec_generator {
  struct promise_type {
    struct awaitable {
      awaitable(promise_type *childp) : child_(childp) {}

      bool await_ready() { return child_ == nullptr; }
      void await_suspend(coro::coroutine_handle<promise_type>) {}
      void await_resume() {}

      promise_type *child_;
    };

    promise_type() : value_(nullptr), root_(this), leaf_(this) {}

    promise_type(const promise_type &) = delete;
    promise_type(promise_type &&) = delete;

    auto get_return_object() noexcept { return rec_generator<T>{*this}; }
    auto initial_suspend() noexcept { return coro::suspend_always{}; }
    auto final_suspend() noexcept { return coro::suspend_always{}; }
    void unhandled_exception() { std::terminate(); }
    void return_void() noexcept {}

    auto yield_value(T &value) noexcept {
      value_ = std::addressof(value);
      return coro::suspend_always{};
    }

    coro::suspend_always yield_value(T &&value) noexcept {
      value_ = std::addressof(value);
      return {};
    }

    auto yield_value(rec_generator &generator) {
      if (generator.promise_ != nullptr) {
        root_->leaf_ = generator.promise_;
        generator.promise_->root_ = root_;
        generator.promise_->leaf_ = this;
        generator.promise_->resume();

        if (!generator.promise_->is_complete())
          return awaitable{generator.promise_};

        root_->leaf_ = this;
      }

      return awaitable{nullptr};
    }

    // Don't allow any use of 'co_await' inside the rec_generator coroutine.
    template <typename U>
    coro::suspend_never await_transform(U &&value) = delete;

    void destroy() noexcept {
      coro::coroutine_handle<promise_type>::from_promise(*this).destroy();
    }

    bool is_complete() noexcept {
      return coro::coroutine_handle<promise_type>::from_promise(*this).done();
    }

    T &value() noexcept {
      assert(this == root_);
      assert(!is_complete());
      return *(leaf_->value_);
    }

    void pull() noexcept {
      assert(this == root_);
      assert(!leaf_->is_complete());

      leaf_->resume();

      while (leaf_ != this && leaf_->is_complete()) {
        leaf_ = leaf_->leaf_;
        leaf_->resume();
      }
    }

  private:
    void resume() noexcept {
      coro::coroutine_handle<promise_type>::from_promise(*this).resume();
    }

    std::add_pointer_t<T> value_;
    promise_type *root_;
    promise_type *leaf_;
  };

  rec_generator() noexcept : promise_(nullptr) {}

  rec_generator(promise_type &promise) noexcept : promise_(&promise) {}

  rec_generator(rec_generator &&other) noexcept : promise_(other.promise_) {
    other.promise_ = nullptr;
  }

  rec_generator(const rec_generator &other) = delete;
  rec_generator &operator=(const rec_generator &other) = delete;

  ~rec_generator() {
    if (promise_ != nullptr)
      promise_->destroy();
  }

  rec_generator &operator=(rec_generator &&other) noexcept {
    if (this == &other)
      return *this;

    if (promise_ != nullptr)
      promise_->destroy();

    promise_ = other.promise_;
    other.promise_ = nullptr;
  }

  T value() { return promise_->value(); }

  bool next() {
    promise_->pull();
    if (promise_->is_complete())
      return false;
    return true;
  }

  void swap(rec_generator &other) noexcept {
    std::swap(promise_, other.promise_);
  }

private:
  promise_type *promise_;
};
