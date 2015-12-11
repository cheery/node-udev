#include <v8.h>
#include <nan.h>

#include <libudev.h>

using namespace v8;

static struct udev *udev;

static void PushProperties(Local<Object> obj, struct udev_device* dev) {
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;
    sysattrs = udev_device_get_properties_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs) {
        const char *name, *value;
        name = udev_list_entry_get_name(entry);
        value = udev_list_entry_get_value(entry);
        if (value != NULL) {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::New<String>(value).ToLocalChecked());
        } else {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::Null());
        }
    }
}

static void PushSystemAttributes(Local<Object> obj, struct udev_device* dev) {
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;
    sysattrs = udev_device_get_sysattr_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs) {
        const char *name, *value;
        name = udev_list_entry_get_name(entry);
        value = udev_device_get_sysattr_value(dev, name);
        if (value != NULL) {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::New<String>(value).ToLocalChecked());
        } else {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::Null());
        }
    }
}

class Monitor : public node::ObjectWrap {
    struct poll_struct {
      Nan::Persistent<Object> monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events) {
        Nan::HandleScope scope;
        poll_struct* data = (poll_struct*)handle->data;
        Local<Object> monitor = Nan::New(data->monitor);
        Monitor* wrapper = ObjectWrap::Unwrap<Monitor>(monitor);
        udev_device* dev = udev_monitor_receive_device(wrapper->mon);

        Local<Object> obj = Nan::New<Object>();
        obj->Set(Nan::New<String>("syspath").ToLocalChecked(), Nan::New<String>(udev_device_get_syspath(dev)).ToLocalChecked());
        PushProperties(obj, dev);

        TryCatch tc;
        Local<Function> emit = monitor->Get(Nan::New<String>("emit").ToLocalChecked()).As<Function>();
        Local<Value> emitArgs[2];
        emitArgs[0] = Nan::New<String>(udev_device_get_action(dev)).ToLocalChecked();
        emitArgs[1] = obj;
        emit->Call(monitor, 2, emitArgs);

        udev_device_unref(dev);
        //if (tc.HasCaught()) node::FatalException(tc);
    };

    static NAN_METHOD(New) {
        Nan::HandleScope scope;
        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        obj->Wrap(info.This());
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        obj->fd = udev_monitor_get_fd(obj->mon);
        obj->poll_handle = handle = new uv_poll_t;
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

    static NAN_METHOD(Close) {
        Nan::HandleScope scope;
        Monitor* obj = ObjectWrap::Unwrap<Monitor>(info.This());
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        return;
    }

    public:
    static void Init(Handle<Object> target) {
        // I do not remember why the functiontemplate was tugged into a persistent.
        static Nan::Persistent<FunctionTemplate> constructor;

        //Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
        Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
        tpl->SetClassName(Nan::New<String>("Monitor").ToLocalChecked());
        tpl->InstanceTemplate()->SetInternalFieldCount(1);
        //NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
        Nan::SetPrototypeMethod(tpl, "close", Close);
        //NanAssignPersistent(constructor, tpl);
        constructor.Reset(tpl);

        //target->Set(
        Nan::Set(
            target,
            Nan::New<String>("Monitor").ToLocalChecked(), 
            //Nan::New(tpl)->GetFunction()
            Nan::GetFunction(Nan::New(tpl)).ToLocalChecked()
        );
    }
};

NAN_METHOD(List) {
    Nan::HandleScope scope;
    //NanScope();
    Local<Array> list = Nan::New<Array>();
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices;
    struct udev_list_entry* entry;
    struct udev_device *dev;

    enumerate = udev_enumerate_new(udev);
    // add match etc. stuff.
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    int i = 0;
    udev_list_entry_foreach(entry, devices) {
        const char *path;
        path = udev_list_entry_get_name(entry);
        dev = udev_device_new_from_syspath(udev, path);
        Local<Object> obj = Nan::New<Object>();
        PushProperties(obj, dev);

        //obj->Set(
        Nan::Set(
            obj,
            Nan::New<String>("syspath").ToLocalChecked(), 
            Nan::New<String>(path).ToLocalChecked()
        );

        //list->Set(i++, obj);
        Nan::Set(list, i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    //NanReturnValue(list);
    info.GetReturnValue().Set(list);
}

NAN_METHOD(GetNodeParentBySyspath) {
    Nan::HandleScope scope;
    //NanScope();
    struct udev_device *dev;
    struct udev_device *parentDev;

    if(!info[0]->IsString()) {
        Nan::ThrowTypeError("first argument must be a string");
    }

    v8::Local<v8::String> string = info[0]->ToString();

    dev = udev_device_new_from_syspath(udev, *Nan::Utf8String(string));
    if (dev == NULL) {
        Nan::ThrowError("device not found");
    }
    parentDev = udev_device_get_parent(dev);
    if(parentDev == NULL) {
        udev_device_unref(dev);
    }

    Local<Object> obj = Nan::New<Object>();
    PushProperties(obj, parentDev);
    const char *path;
    path = udev_device_get_syspath(parentDev);

    //obj->Set(
    Nan::Set(
        obj,
        Nan::New<String>("syspath").ToLocalChecked(), 
        Nan::New<String>(path).ToLocalChecked()
    );

    udev_device_unref(dev);

    //NanReturnValue(obj);
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(GetSysattrBySyspath) {
    Nan::HandleScope scope;
    //NanScope();
    struct udev_device *dev;

    if(!info[0]->IsString()) {
      Nan::ThrowTypeError("first argument must be a string");
        //NanReturnNull();
    }

    v8::Local<v8::String> string = info[0]->ToString();

    dev = udev_device_new_from_syspath(udev, *Nan::Utf8String(string));
    if (dev == NULL) {
      Nan::ThrowError("device not found");
    }

    Local<Object> obj = Nan::New<Object>();
    PushSystemAttributes(obj, dev);
    obj->Set(Nan::New<String>("syspath").ToLocalChecked(), string);
    udev_device_unref(dev);

    //NanReturnValue(obj);
    info.GetReturnValue().Set(obj);
}

static void Init(Handle<Object> target) {
    udev = udev_new();

    if (!udev) {
      Nan::ThrowError("Can't create udev\n");
    }

    Nan::Set(
        target,
        Nan::New<String>("list").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(List)).ToLocalChecked()
    );
    Nan::Set(
        target,
        Nan::New<String>("getNodeParentBySyspath").ToLocalChecked(),
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(GetNodeParentBySyspath)).ToLocalChecked()
    );
    //target->Set(
    Nan::Set(
        target,
        Nan::New<String>("getSysattrBySyspath").ToLocalChecked(),
        //Nan::New<FunctionTemplate>(GetSysattrBySyspath)->GetFunction());
        Nan::GetFunction(Nan::New<v8::FunctionTemplate>(GetSysattrBySyspath)).ToLocalChecked()
    );

    Monitor::Init(target);
}
NODE_MODULE(udev, Init)
