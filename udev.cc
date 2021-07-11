#include <v8.h>
#include <napi.h>
#include <uv.h>

#include <libudev.h>

using namespace Napi;

static struct udev *udev;

static void PushProperties(Napi::Object obj, struct udev_device* dev) {
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

class Monitor : public node::ObjectWrap {
    struct poll_struct {
      Napi::ObjectReference monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events) {
        Napi::HandleScope scope(env);
        poll_struct* data = (poll_struct*)handle->data;
        Napi::Object monitor = Napi::New(env, data->monitor);
        Monitor* wrapper = ObjectWrap::Unwrap<Monitor>(monitor);
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
        Napi::HandleScope scope(env);
        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        obj->Wrap(info.This());
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        obj->fd = udev_monitor_get_fd(obj->mon);
        obj->poll_handle = handle = new uv_poll_t;

        if (info[0].IsString()) {
            Napi::String subsystem = info[0].ToString();
            int r;
            r = udev_monitor_filter_add_match_subsystem_devtype(obj->mon,
                    subsystem->As<Napi::String>().Utf8Value().c_str(), NULL);
            if (r < 0) {
                Napi::Error::New(env, "adding the subsystem filter failed").ThrowAsJavaScriptException();

            }
        }

        udev_monitor_enable_receiving(obj->mon);
        poll_struct* data = new poll_struct;
        //NanAssignPersistent(data->monitor, info.This());
        data->monitor.Reset(info.This());
        handle->data = data;
        uv_poll_init(uv_default_loop(), obj->poll_handle, obj->fd);
        uv_poll_start(obj->poll_handle, UV_READABLE, on_handle_event);
        //NanReturnThis();
        return;
    }

    static void on_handle_close(uv_handle_t *handle) {
        poll_struct* data = (poll_struct*)handle->data;
        //NanDisposePersistent(data->monitor);
        data->monitor.Reset();
        delete data;
        delete handle;
    }

    static Napi::Value Close(const Napi::CallbackInfo& info) {
        Napi::HandleScope scope(env);
        Monitor* obj = ObjectWrap::Unwrap<Monitor>(info.This());
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        return;
    }

    public:
    static void Init(Handle<Object> target) {
        // I do not remember why the functiontemplate was tugged into a persistent.
        static Napi::FunctionReference constructor;

        //Napi::FunctionReference tpl = Napi::Function::New(env, New);
        Local<Napi::FunctionReference> tpl = Napi::Function::New(env, New);
        tpl->SetClassName(Napi::String::New(env, "Monitor"));

        //NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
        InstanceMethod("close", &Close),
        //NanAssignPersistent(constructor, tpl);
        constructor.Reset(tpl);

        //target.Set(
        (
            target).Set(Napi::String::New(env, "Monitor"), 
            Napi::GetFunction(tpl)
        );
    }
};

Napi::Value List(const Napi::CallbackInfo& info) {
    Napi::HandleScope scope(env);
    //NanScope();
    Napi::Array list = Napi::Array::New(env);
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices;
    struct udev_list_entry* entry;
    struct udev_device *dev;

    enumerate = udev_enumerate_new(udev);
    // add match etc. stuff.
    if(info[0].IsString()) {
        Napi::String subsystem = info[0].ToString();
        udev_enumerate_add_match_subsystem(enumerate, subsystem->As<Napi::String>().Utf8Value().c_str());
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

        //obj.Set(
        (
            obj).Set(Napi::String::New(env, "syspath"), 
            Napi::String::New(env, path)
        );

        //list.Set(i++, obj);
        (list).Set(i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    //NanReturnValue(list);
    return list;
}

Napi::Value GetNodeParentBySyspath(const Napi::CallbackInfo& info) {
    Napi::HandleScope scope(env);
    //NanScope();
    struct udev_device *dev;
    struct udev_device *parentDev;

    if(!info[0].IsString()) {
        Napi::TypeError::New(env, "first argument must be a string").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::String string = info[0].ToString();

    dev = udev_device_new_from_syspath(udev, string->As<Napi::String>().Utf8Value().c_str());
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
    Napi::HandleScope scope(env);
    //NanScope();
    struct udev_device *dev;

    if(!info[0].IsString()) {
      Napi::TypeError::New(env, "first argument must be a string").ThrowAsJavaScriptException();

        //NanReturnNull();
        return;
    }

    Napi::String string = info[0].ToString();

    dev = udev_device_new_from_syspath(udev, string->As<Napi::String>().Utf8Value().c_str());
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

static void Init(Handle<Object> target) {
    udev = udev_new();

    if (!udev) {
      Napi::Error::New(env, "Can't create udev\n").ThrowAsJavaScriptException();
      return env.Null();
    }

    (
        target).Set(Napi::String::New(env, "list"),
        Napi::GetFunction(Napi::Function::New(env, List))
    );
    (
        target).Set(Napi::String::New(env, "getNodeParentBySyspath"),
        Napi::GetFunction(Napi::Function::New(env, GetNodeParentBySyspath))
    );
    //target.Set(
    (
        target).Set(Napi::String::New(env, "getSysattrBySyspath"),
        //Napi::Function::New(env, GetSysattrBySyspath);());
        Napi::GetFunction(Napi::Function::New(env, GetSysattrBySyspath))
    );

    Monitor::Init(env, target, module);
}
NODE_API_MODULE(udev, Init)
