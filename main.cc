#include <napi.h>

#include "udev.h"

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  exports.Set("list", Napi::Function::New(env, Udev::List));
  exports.Set("getNodeParentBySyspath",
              Napi::Function::New(env, Udev::GetNodeParentBySyspath));
  exports.Set("getSysattrBySyspath",
              Napi::Function::New(env, Udev::GetSysattrBySyspath));

  return Monitor::Init(env, exports);
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
