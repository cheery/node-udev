#include <v8.h>
#include <napi.h>
#include <uv.h>

#include <libudev.h>

using namespace Napi;

static struct udev *udev;

static void PushProperties(Napi::Object obj, struct udev_device* dev) {
    Napi::Env env = obj.Env();
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;
    sysattrs = udev_device_get_properties_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs) {
        const char *name, *value;
        name = udev_list_entry_get_name(entry);
        value = udev_list_entry_get_value(entry);
        if (value != NULL) {
            obj.Set(Napi::String::New(env, name), Napi::String::New(env, value));
        } else {
            obj.Set(Napi::String::New(env, name), env.Null());
        }
    }
}

static void PushSystemAttributes(Napi::Object obj, struct udev_device* dev) {
    Napi::Env env = obj.Env();
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;
    sysattrs = udev_device_get_sysattr_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs) {
        const char *name, *value;
        name = udev_list_entry_get_name(entry);
        value = udev_device_get_sysattr_value(dev, name);
        if (value != NULL) {
            obj.Set(Napi::String::New(env, name), Napi::String::New(env, value));
        } else {
            obj.Set(Napi::String::New(env, name), env.Null());
        }
    }
}

class Monitor : public Napi::ObjectWrap<Monitor> {

    Monitor();

    struct poll_struct {
      Napi::ObjectReference monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events) {
        Napi::HandleScope scope(env);
        poll_struct* data = (poll_struct*)handle->data;
        Napi::Object monitor = Napi::Object::New(env, data->monitor);
        Monitor* wrapper = ObjectWrap::Unwrap(monitor);
        udev_device* dev = udev_monitor_receive_device(wrapper->mon);
        if (dev == NULL) {
            return;
        }

        Napi::Object obj = Napi::Object::New(env);
        obj.Set(Napi::String::New(env, "syspath"), Napi::String::New(env, udev_device_get_syspath(dev)));
        PushProperties(obj, dev);

        Napi::Function emit = monitor->Get(Napi::String::New(env, "emit")).As<Napi::Function>();
        Napi::Value emitArgs[2];
        emitArgs[0] = Napi::String::New(env, udev_device_get_action(dev));
        emitArgs[1] = obj;
        emit->Call(monitor, 2, emitArgs);

        udev_device_unref(dev);
    };

    static Napi::Value New(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::HandleScope scope(env);
        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        // obj->Wrap(info.This());
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        obj->fd = udev_monitor_get_fd(obj->mon);
        obj->poll_handle = handle = new uv_poll_t;

        if (info[0].IsString()) {
            Napi::String subsystem = info[0].As<Napi::String>();
            int r;
            r = udev_monitor_filter_add_match_subsystem_devtype(obj->mon,
                    subsystem.Utf8Value().c_str(), NULL);
            if (r < 0) {
                Napi::Error::New(env, "adding the subsystem filter failed").ThrowAsJavaScriptException();

            }
        }

        udev_monitor_enable_receiving(obj->mon);
        poll_struct* data = new poll_struct;
        data->monitor.Reset();
        handle->data = data;
        uv_poll_init(uv_default_loop(), obj->poll_handle, obj->fd);
        uv_poll_start(obj->poll_handle, UV_READABLE, on_handle_event);
        return;
    }

    static void on_handle_close(uv_handle_t *handle) {
        poll_struct* data = (poll_struct*)handle->data;
        data->monitor.Reset();
        delete data;
        delete handle;
    }

    static Napi::Value Close(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::HandleScope scope(env);
        Monitor* obj = &info[0].As<Monitor>();
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        return env.Null();
    }

    public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::HandleScope scope(env);
        Napi::FunctionReference constructor;
        Napi::Function func = DefineClass(env, "Monitor", {
            StaticMethod("close", &Monitor::Close)
        });

        constructor = Napi::Persistent(func);
        constructor.Reset(func);

        exports.Set(Napi::String::New(env, "Monitor"), func);
        return exports;
    }
};

Napi::Value List(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    Napi::Array list = Napi::Array::New(env);
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices;
    struct udev_list_entry* entry;
    struct udev_device *dev;

    enumerate = udev_enumerate_new(udev);
    // add match etc. stuff.
    if(info[0].IsString()) {
        Napi::String subsystem = info[0].As<Napi::String>();
        udev_enumerate_add_match_subsystem(enumerate, subsystem.Utf8Value().c_str());
    }
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    int i = 0;
    udev_list_entry_foreach(entry, devices) {
        const char *path;
        path = udev_list_entry_get_name(entry);
        dev = udev_device_new_from_syspath(udev, path);
        Napi::Object obj = Napi::Object::New(env);
        PushProperties(obj, dev);

        obj.Set(Napi::String::New(env, "syspath"), Napi::String::New(env, path));

        list.Set(i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    return list;
}

Napi::Value GetNodeParentBySyspath(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    //NanScope();
    struct udev_device *dev;
    struct udev_device *parentDev;

    if(!info[0].IsString()) {
        Napi::TypeError::New(env, "first argument must be a string").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::String string = info[0].As<Napi::String>();

    dev = udev_device_new_from_syspath(udev, string.Utf8Value().c_str());
    if (dev == NULL) {
        Napi::Error::New(env, "device not found").ThrowAsJavaScriptException();
        return env.Null();
    }

    parentDev = udev_device_get_parent(dev);
    if(parentDev == NULL) {
        udev_device_unref(dev);
    }

    Napi::Object obj = Napi::Object::New(env);
    PushProperties(obj, parentDev);
    const char *path;
    path = udev_device_get_syspath(parentDev);

    if(!path) {
        Napi::Error::New(env, "udev returned null syspath").ThrowAsJavaScriptException();
        return env.Null();
    }

    //obj.Set(
    (
        obj).Set(Napi::String::New(env, "syspath"), 
        Napi::String::New(env, path)
    );

    udev_device_unref(dev);

    //NanReturnValue(obj);
    return obj;
}

Napi::Value GetSysattrBySyspath(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    //NanScope();
    struct udev_device *dev;

    if(!info[0].IsString()) {
      Napi::TypeError::New(env, "first argument must be a string").ThrowAsJavaScriptException();

        //NanReturnNull();
        return;
    }

    Napi::String string = info[0].As<Napi::String>();

    dev = udev_device_new_from_syspath(udev, string.Utf8Value().c_str());
    if (dev == NULL) {
      Napi::Error::New(env, "device not found").ThrowAsJavaScriptException();
      return env.Null();
    }

    Napi::Object obj = Napi::Object::New(env);
    PushSystemAttributes(obj, dev);
    obj.Set(Napi::String::New(env, "syspath"), string);
    udev_device_unref(dev);

    //NanReturnValue(obj);
    return obj;
}

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    udev = udev_new();

    if (!udev) {
      Napi::Error::New(env, "Can't create udev\n").ThrowAsJavaScriptException();
      return;
    }

    exports.Set(Napi::String::New(env, "list"), Napi::Function::New(env, List));
    exports.Set(Napi::String::New(env, "getNodeParentBySyspath"), Napi::Function::New(env, GetNodeParentBySyspath));
    exports.Set(Napi::String::New(env, "getSysattrBySyspath"), Napi::Function::New(env, GetSysattrBySyspath));

    Monitor::Init(env, exports);
    return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
