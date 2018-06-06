// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/atom_bindings.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include "atom/common/atom_version.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "atom/common/node_includes.h"
#include "base/logging.h"
#include "base/process/process_metrics.h"
#include "chrome/common/chrome_version.h"
#include "native_mate/dictionary.h"

namespace atom {

namespace {

// Dummy class type that used for crashing the program.
struct DummyClass { bool crash; };

void Crash() {
  static_cast<DummyClass*>(nullptr)->crash = true;
}

void Hang() {
  for (;;)
    base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
}

v8::Local<v8::Value> GetSystemMemoryInfo(v8::Isolate* isolate,
                                         mate::Arguments* args) {
  base::SystemMemoryInfoKB mem_info;
  if (!base::GetSystemMemoryInfo(&mem_info)) {
    args->ThrowError("Unable to retrieve system memory information");
    return v8::Undefined(isolate);
  }

  mate::Dictionary dict = mate::Dictionary::CreateEmpty(isolate);
  dict.Set("total", mem_info.total);
#if !defined(OS_WIN)
  dict.Set("free", mem_info.free);
#endif
#if defined(OS_WIN)
  dict.Set("availPhys", mem_info.avail_phys);
#endif

  // NB: These return bogus values on macOS
#if !defined(OS_MACOSX)
  dict.Set("swapTotal", mem_info.swap_total);
  dict.Set("swapFree", mem_info.swap_free);
#endif

  return dict.GetHandle();
}

// Called when there is a fatal error in V8, we just crash the process here so
// we can get the stack trace.
void FatalErrorCallback(const char* location, const char* message) {
  LOG(ERROR) << "Fatal error in V8: " << location << " " << message;
  Crash();
}

void Log(const base::string16& message) {
  std::cout << message << std::flush;
}

}  // namespace


AtomBindings::AtomBindings() {
  uv_async_init(uv_default_loop(), &call_next_tick_async_, OnCallNextTick);
  call_next_tick_async_.data = this;
}

AtomBindings::~AtomBindings() {
}

void AtomBindings::BindTo(v8::Isolate* isolate,
                          v8::Local<v8::Object> process) {
  isolate->SetFatalErrorHandler(FatalErrorCallback);

  mate::Dictionary dict(isolate, process);
  dict.SetMethod("crash", &Crash);
  dict.SetMethod("hang", &Hang);
  dict.SetMethod("log", &Log);
  dict.SetMethod("getSystemMemoryInfo", &GetSystemMemoryInfo);
#if defined(OS_POSIX)
  dict.SetMethod("increaseFdLimitTo", &base::IncreaseFdLimitTo);
#endif
  dict.SetMethod("activateUvLoop",
      base::Bind(&AtomBindings::ActivateUVLoop, base::Unretained(this)));

#if defined(MAS_BUILD)
  dict.Set("mas", true);
#endif

  mate::Dictionary versions;
  if (dict.Get("versions", &versions)) {
    versions.Set(PRODUCT_FULLNAME_STRING, ATOM_VERSION_STRING);
    versions.Set("atom-shell", ATOM_VERSION_STRING);  // For compatibility.
    versions.Set("chrome", CHROME_VERSION_STRING);
  }
}

void AtomBindings::ActivateUVLoop(v8::Isolate* isolate) {
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (std::find(pending_next_ticks_.begin(), pending_next_ticks_.end(), env) !=
      pending_next_ticks_.end())
    return;

  pending_next_ticks_.push_back(env);
  uv_async_send(&call_next_tick_async_);
}

// static
void AtomBindings::OnCallNextTick(uv_async_t* handle) {
  AtomBindings* self = static_cast<AtomBindings*>(handle->data);
  for (std::list<node::Environment*>::const_iterator it =
           self->pending_next_ticks_.begin();
       it != self->pending_next_ticks_.end(); ++it) {
    node::Environment* env = *it;
    // KickNextTick, copied from node.cc:
    node::Environment::AsyncCallbackScope callback_scope(env);
    if (callback_scope.in_makecallback())
      continue;
    node::Environment::TickInfo* tick_info = env->tick_info();
    if (tick_info->length() == 0)
      env->isolate()->RunMicrotasks();
    v8::Local<v8::Object> process = env->process_object();
    if (tick_info->length() == 0)
      tick_info->set_index(0);
    env->tick_callback_function()->Call(process, 0, nullptr).IsEmpty();
  }

  self->pending_next_ticks_.clear();
}

}  // namespace atom
