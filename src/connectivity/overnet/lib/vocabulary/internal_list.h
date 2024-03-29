// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//#define OVERNET_UNIQUE_PTR_NODE

namespace overnet {

template <class T>
class InternalListNode;

template <class T, InternalListNode<T> T::*NodePtr>
class InternalList;

template <class T>
class InternalListNode {
  template <class U, InternalListNode<U> U::*NodePtr>
  friend class InternalList;

 public:
  InternalListNode() = default;
  InternalListNode(const InternalListNode&) = delete;
  InternalListNode& operator=(const InternalListNode&) = delete;
#ifdef OVERNET_UNIQUE_PTR_NODE
  ~InternalListNode() { assert(inner_->owner_ == nullptr); }
#else
  ~InternalListNode() { assert(owner_ == nullptr); }
#endif

 private:
#ifdef OVERNET_UNIQUE_PTR_NODE
  struct Inner {
#endif
    T* next_ = nullptr;
    T* prev_ = nullptr;
#ifndef NDEBUG
    std::unique_ptr<void*> owner_;
#endif
#ifdef OVERNET_UNIQUE_PTR_NODE
  };
  std::unique_ptr<Inner> inner_ = std::make_unique<Inner>();
#endif
};

template <class T, InternalListNode<T> T::*NodePtr>
class InternalList {
 public:
  InternalList() = default;
  InternalList(const InternalList&) = delete;
  InternalList& operator=(const InternalList&) = delete;
  ~InternalList() {
    while (head_)
      Remove(head_);
  }

  T* Front() { return head_; }
  bool Empty() const { return head_ == nullptr; }

  bool Contains(T* p) {
    for (auto q : *this)
      if (p == q)
        return true;
    return false;
  }

  void PushBack(T* p) {
#ifndef NDEBUG
    assert(Node(p)->owner_ == nullptr);
    Node(p)->owner_ = std::make_unique<void*>(this);
#endif
    assert(Node(p)->next_ == nullptr);
    assert(Node(p)->prev_ == nullptr);
    if (tail_ == nullptr) {
      assert(head_ == nullptr);
      head_ = tail_ = p;
    } else {
      assert(Node(tail_)->next_ == nullptr);
      Node(p)->prev_ = tail_;
      Node(tail_)->next_ = p;
      tail_ = p;
    }
    size_++;
  }

  void PushFront(T* p) {
#ifndef NDEBUG
    assert(Node(p)->owner_ == nullptr);
    Node(p)->owner_ = std::make_unique<void*>(this);
#endif
    assert(Node(p)->next_ == nullptr);
    assert(Node(p)->prev_ == nullptr);
    if (head_ == nullptr) {
      assert(tail_ == nullptr);
      head_ = tail_ = p;
    } else {
      assert(Node(head_)->prev_ == nullptr);
      Node(p)->next_ = head_;
      Node(head_)->prev_ = p;
      head_ = p;
    }
    size_++;
  }

  void Remove(T* p) {
#ifndef NDEBUG
    assert(*Node(p)->owner_ == this);
    Node(p)->owner_.reset();
#endif
    if (p == head_) {
      head_ = Node(p)->next_;
      if (head_ == nullptr) {
        assert(p == tail_);
        head_ = tail_ = nullptr;
      } else {
        Node(head_)->prev_ = nullptr;
      }
    } else if (p == tail_) {
      tail_ = Node(p)->prev_;
      assert(tail_ != nullptr);  // otherwise p == head_ at start is true
      Node(tail_)->next_ = nullptr;
    } else {
      Node(Node(p)->next_)->prev_ = Node(p)->prev_;
      Node(Node(p)->prev_)->next_ = Node(p)->next_;
    }
    Node(p)->next_ = Node(p)->prev_ = nullptr;
    size_--;
  }

  T* PopFront() {
    T* p = Front();
    if (p)
      Remove(p);
    return p;
  }

  class Iterator {
   public:
    Iterator(T* node) : p_(node) {}
    bool operator!=(Iterator other) const { return p_ != other.p_; }
    Iterator& operator++() {
      p_ = Node(p_)->next_;
      return *this;
    }
    T* operator*() { return p_; }
    T* operator->() { return p_; }

   private:
    T* p_;
  };

  Iterator begin() { return Iterator(head_); }
  Iterator end() { return Iterator(nullptr); }

  size_t Size() const { return size_; }

 private:
  static auto* Node(T* p) {
#ifdef OVERNET_UNIQUE_PTR_NODE
    return (p->*NodePtr).inner_.get();
#else
    return &(p->*NodePtr);
#endif
  }

  T* head_ = nullptr;
  T* tail_ = nullptr;
  size_t size_ = 0;
};

}  // namespace overnet