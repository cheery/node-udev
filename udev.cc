#include <napi.h>
#include <uv.h>

#include "udev.h"
#include <libudev.h>

Napi::FunctionReference Monitor::constructor;

namespace detail {
void unref_udev(udev *up) { udev_unref(up); }
void unref_udev_device(udev_device *dev) { udev_device_unref(dev); }
void unref_udev_enumerate(udev_enumerate *e) { udev_enumerate_unref(e); }
} // namespace detail
using udev_ptr = typename std::unique_ptr<udev, decltype(&detail::unref_udev)>;
using udev_device_ptr =
    typename std::unique_ptr<udev_device, decltype(&detail::unref_udev_device)>;
using udev_enumerate_ptr =
    typename std::unique_ptr<udev_enumerate,
                             decltype(&detail::unref_udev_enumerate)>;

Monitor::Monitor(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Monitor>(info) {

  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  emit_ = Napi::Persistent(
      info.This().As<Napi::Object>().Get("emit").As<Napi::Function>());
  instance_ = udev_new();
  mon_ = udev_monitor_new_from_netlink(instance_, "udev");
  fd_ = udev_monitor_get_fd(mon_);
  poll_handle_ = new uv_poll_t;

  std::vector<std::string> subsystem_filters = {};
  std::vector<std::string> tag_filters = {};
  if (info[0].IsString()) {
    Napi::String subsystem = info[0].ToString();
    subsystem_filters.push_back(subsystem.Utf8Value());
  } else if (info[0].IsObject()) {
    Napi::Object filters = info[0].ToObject();
    if (filters.Has("subsystems")) {
      Napi::Array subsystems = filters.Get("subsystems").As<Napi::Array>();
      for (uint32_t i = 0; i < subsystems.Length(); i++) {
        subsystem_filters.push_back(
            static_cast<Napi::Value>(subsystems[i]).ToString().Utf8Value());
      }
    }
    if (filters.Has("tags")) {
      Napi::Array tags = filters.Get("tags").As<Napi::Array>();
      for (uint32_t i = 0; i < tags.Length(); i++) {
        tag_filters.push_back(
            static_cast<Napi::Value>(tags[i]).ToString().Utf8Value());
      }
    }
  }

  for (std::string subsystem_filter : subsystem_filters) {
    int r = udev_monitor_filter_add_match_subsystem_devtype(
        mon_, subsystem_filter.c_str(), NULL);
    if (r < 0) {
      Napi::Error::New(env, "adding subsystem filter failed")
          .ThrowAsJavaScriptException();
    }
  }
  for (std::string tag_filter : tag_filters) {
    int r = udev_monitor_filter_add_match_tag(mon_, tag_filter.c_str());
    if (r < 0) {
      Napi::Error::New(env, "adding tag filter failed")
          .ThrowAsJavaScriptException();
    }
  }

  udev_monitor_enable_receiving(mon_);
  poll_handle_->data =
      new data_holder{Napi::Persistent(info.This().As<Napi::Object>())};

  uv_poll_init(uv_default_loop(), poll_handle_, fd_);
  uv_poll_start(poll_handle_, UV_READABLE, Monitor::on_handle_event);
}

Monitor::~Monitor() {
  uv_poll_stop(poll_handle_);
  uv_close((uv_handle_t *)poll_handle_, on_handle_close);
  udev_monitor_unref(mon_);
  udev_unref(instance_);
}

void Monitor::on_handle_event(uv_poll_t *handle, int status, int events) {
  auto data = static_cast<data_holder *>(handle->data);
  Napi::Env env = data->monitor.Env();
  Napi::HandleScope scope(env);

  Monitor *wrapper = Napi::ObjectWrap<Monitor>::Unwrap(data->monitor.Value());

  udev_device_ptr dev(udev_monitor_receive_device(wrapper->mon_),
                      &detail::unref_udev_device);
  if (!dev) {
    return;
  }

  Napi::String device =
      Napi::String::New(env, udev_device_get_action(dev.get()));
  Napi::Object obj = Napi::Object::New(env);
  obj.Set("syspath",
          Napi::String::New(env, udev_device_get_syspath(dev.get())));
  Udev::PushProperties(env, obj, dev.get());
  wrapper->emit_.Call(data->monitor.Value(), {device, obj});
}

void Monitor::on_handle_close(uv_handle_t *handle) {
  auto data = static_cast<data_holder *>(handle->data);
  data->monitor.Reset();
  delete data;
  delete handle;
}

Napi::Value Monitor::Close(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  Napi::HandleScope scope(env);
  uv_poll_stop(poll_handle_);
  uv_close((uv_handle_t *)poll_handle_, on_handle_close);
  udev_monitor_unref(mon_);
  return env.Undefined();
}

Napi::Object Monitor::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func =
      DefineClass(env, "Monitor", {InstanceMethod("close", &Monitor::Close)});
  Monitor::constructor = Napi::Persistent(func);
  Monitor::constructor.SuppressDestruct();
  exports.Set("Monitor", func);
  // env.SetInstanceData<Napi::FunctionReference>(constructor);

  return exports;
}

namespace Udev {

void PushProperties(Napi::Env &env, Napi::Object obj, struct udev_device *dev) {
  struct udev_list_entry *entry;
  struct udev_list_entry *sysattrs = udev_device_get_properties_list_entry(dev);
  udev_list_entry_foreach(entry, sysattrs) {
    const char *name = udev_list_entry_get_name(entry);
    const char *value = udev_list_entry_get_value(entry);
    if (value != NULL) {
      obj.Set(name, Napi::String::New(env, value));
    } else {
      obj.Set(name, env.Null());
    }
  }
}

Napi::Value List(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  udev_ptr instance(udev_new(), &detail::unref_udev);

  Napi::Array list = Napi::Array::New(env);
  struct udev_list_entry *devices;
  struct udev_list_entry *entry;

  udev_enumerate_ptr enumerate(udev_enumerate_new(instance.get()),
                               &detail::unref_udev_enumerate);
  // add match etc. stuff.
  std::vector<std::string> subsystem_filters = {};
  std::vector<std::string> tag_filters = {};
  if (info[0].IsString()) {
    Napi::String subsystem = info[0].ToString();
    subsystem_filters.push_back(subsystem.Utf8Value());
  } else if (info[0].IsObject()) {
    Napi::Object filters = info[0].ToObject();
    if (filters.Has("subsystems")) {
      Napi::Array subsystems = filters.Get("subsystems").As<Napi::Array>();
      for (uint32_t i = 0; i < subsystems.Length(); i++) {
        subsystem_filters.push_back(
            static_cast<Napi::Value>(subsystems[i]).ToString().Utf8Value());
      }
    }
    if (filters.Has("tags")) {
      Napi::Array tags = filters.Get("tags").As<Napi::Array>();
      for (uint32_t i = 0; i < tags.Length(); i++) {
        tag_filters.push_back(
            static_cast<Napi::Value>(tags[i]).ToString().Utf8Value());
      }
    }
  }

  for (std::string subsystem_filter : subsystem_filters) {
    int r = udev_enumerate_add_match_subsystem(
        enumerate.get(), subsystem_filter.c_str());
    if (r < 0) {
      Napi::Error::New(env, "adding subsystem filter failed")
          .ThrowAsJavaScriptException();
    }
  }
  for (std::string tag_filter : tag_filters) {
    int r = udev_enumerate_add_match_tag(enumerate.get(), tag_filter.c_str());
    if (r < 0) {
      Napi::Error::New(env, "adding tag filter failed")
          .ThrowAsJavaScriptException();
    }
  }

  if (info[0].IsString()) {
    Napi::String subsystem = info[0].ToString();
    udev_enumerate_add_match_subsystem(enumerate.get(),
                                       subsystem.Utf8Value().c_str());
  }
  udev_enumerate_scan_devices(enumerate.get());
  devices = udev_enumerate_get_list_entry(enumerate.get());

  int index = 0;
  udev_list_entry_foreach(entry, devices) {
    const char *path = udev_list_entry_get_name(entry);
    udev_device_ptr dev(udev_device_new_from_syspath(instance.get(), path),
                        &detail::unref_udev_device);
    Napi::Object obj = Napi::Object::New(env);
    Udev::PushProperties(env, obj, dev.get());

    obj.Set("syspath", Napi::String::New(env, path));
    list.Set(index++, obj);
  }

  return list;
}

Napi::Value GetNodeParentBySyspath(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  udev_ptr instance(udev_new(), &detail::unref_udev);

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "first argument must be a string")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::String string = info[0].ToString();

  udev_device_ptr dev(
      udev_device_new_from_syspath(instance.get(), string.Utf8Value().c_str()),
      &detail::unref_udev_device);

  if (!dev) {
    Napi::Error::New(env, "device not found").ThrowAsJavaScriptException();
    return env.Null();
  }

  udev_device *parentDev = udev_device_get_parent(dev.get());

  if (!parentDev) {
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);

  PushProperties(env, obj, parentDev);

  const char *path = udev_device_get_syspath(parentDev);

  if (!path) {
    Napi::Error::New(env, "udev returned null syspath")
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  obj.Set("syspath", Napi::String::New(env, path));

  return obj;
}

void PushSystemAttributes(Napi::Env &env, Napi::Object obj,
                          struct udev_device *dev) {
  struct udev_list_entry *sysattrs;
  struct udev_list_entry *entry;
  sysattrs = udev_device_get_sysattr_list_entry(dev);
  udev_list_entry_foreach(entry, sysattrs) {
    const char *name = udev_list_entry_get_name(entry);
    const char *value = udev_device_get_sysattr_value(dev, name);
    if (value != nullptr) {
      obj.Set(name, Napi::String::New(env, value));
    } else {
      obj.Set(name, env.Null());
    }
  }
}

Napi::Value GetSysattrBySyspath(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  udev_ptr instance(udev_new(), &detail::unref_udev);

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "first argument must be a string")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::String string = info[0].ToString();

  udev_device_ptr dev(
      udev_device_new_from_syspath(instance.get(), string.Utf8Value().c_str()),
      &detail::unref_udev_device);
  if (!dev) {
    Napi::Error::New(env, "device not found").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  PushSystemAttributes(env, obj, dev.get());
  obj.Set(Napi::String::New(env, "syspath"), string);
  return obj;
}
} // namespace Udev
