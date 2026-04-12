/*
MIT License

Copyright (c) 2025 Alexandre GARCIN

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "StatePromise.h"

StatePromise::~StatePromise() {
   // Reject the promise on destruction to unblock any waiting coroutines
   Done();
}

WPromise<void>
StatePromise::WaitReady() const {
   return *ready_promise_;
}

WPromise<void>
StatePromise::WaitDone() const {
   return *done_promise_;
}

WPromise<void>
StatePromise::Wait() const {
   return promise::Race(*done_promise_, *ready_promise_);
}

void
StatePromise::Ready() {
   done_promise_.Reset();
   ready_promise_.Notify();
}

void
StatePromise::Done() {
   done_promise_.Reject<CVPromise::End>();

   if (ready_promise_->Resolved()) {
      ready_promise_.Reset();
      ready_promise_.Reject<CVPromise::End>();
   } else {
      ready_promise_.Reject<CVPromise::End>();
   }
}

void
StatePromise::Reset() {
   ready_promise_.Reset();
   done_promise_.Reset();
}
