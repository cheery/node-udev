#ifndef __UDEV_H__
#define __UDEV_H__
#include <libudev.h>
#include <napi.h>
#include <uv.h>

class Monitor : public Napi::ObjectWrap<Monitor> {
  struct data_holder {
    Napi::ObjectReference monitor;
  };

  uv_poll_t *poll_handle_ = nullptr;
  udev_monitor *mon_ = nullptr;
  udev *instance_ = nullptr;
  int fd_ = 0;
  static Napi::FunctionReference constructor;
  Napi::FunctionReference emit_;

public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  Monitor(const Napi::CallbackInfo &info);
  ~Monitor();

private:
  static void on_handle_event(uv_poll_t *handle, int status, int events);
  static void on_handle_close(uv_handle_t *handle);

  Napi::Value Close(const Napi::CallbackInfo &info);
};

namespace Udev {
void PushProperties(Napi::Env &env, Napi::Object obj, struct udev_device *dev);
void PushSystemAttributes(Napi::Env &env, Napi::Object obj,
                          struct udev_device *dev);

Napi::Value List(const Napi::CallbackInfo &info);
Napi::Value GetNodeParentBySyspath(const Napi::CallbackInfo &info);
Napi::Value GetSysattrBySyspath(const Napi::CallbackInfo &info);
} // namespace Udev
#endif // __UDEV_H__