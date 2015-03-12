#include <v8.h>
#include <node.h>
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
            obj->Set(NanNew<String>(name), NanNew<String>(value));
        } else {
            obj->Set(NanNew<String>(name), NanNull());
        }
    }
}

class Monitor : public node::ObjectWrap {
    struct poll_struct {
        Persistent<Object> monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events) {
        HandleScope scope;
        poll_struct* data = (poll_struct*)handle->data;
        Monitor* wrapper = ObjectWrap::Unwrap<Monitor>(data->monitor);
        udev_device* dev = udev_monitor_receive_device(wrapper->mon);

        Local<Object> obj = NanNew<Object>();
        obj->Set(NanNew<String>("syspath"), NanNew<String>(udev_device_get_syspath(dev)));
        PushProperties(obj, dev);

        TryCatch tc;
        Local<Function> emit = data->monitor->Get(NanNew<String>("emit")).As<Function>();
        Local<Value> emitArgs[2];
        emitArgs[0] = NanNew<String>(udev_device_get_action(dev));
        emitArgs[1] = obj;
        emit->Call(data->monitor, 2, emitArgs);

        udev_device_unref(dev);
        if (tc.HasCaught()) node::FatalException(tc);
    };

    static NAN_METHOD(New) {
        NanScope();
        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        obj->Wrap(args.This());
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        obj->fd = udev_monitor_get_fd(obj->mon);
        obj->poll_handle = handle = new uv_poll_t;
        udev_monitor_enable_receiving(obj->mon);
        poll_struct* data = new poll_struct;
        NanAssignPersistent(data->monitor, args.This());
        handle->data = data;
        uv_poll_init(uv_default_loop(), obj->poll_handle, obj->fd);
        uv_poll_start(obj->poll_handle, UV_READABLE, on_handle_event);
        NanReturnThis();
    }

    static void on_handle_close(uv_handle_t *handle) {
        poll_struct* data = (poll_struct*)handle->data;
        data->monitor.Dispose();
        delete data;
        delete handle;
    }

    static NAN_METHOD(Close) {
        NanScope();
        Monitor* obj = ObjectWrap::Unwrap<Monitor>(args.This());
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        NanReturnUndefined();
    }

    public:
    static void Init(Handle<Object> target) {
        static Persistent<FunctionTemplate> constructor;
        Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
        tpl->SetClassName(NanNew<String>("Monitor"));
        tpl->InstanceTemplate()->SetInternalFieldCount(1);
        NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
        NanAssignPersistent(constructor, tpl);
        target->Set(NanNew<String>("Monitor"), constructor->GetFunction());
    }
};

NAN_METHOD(List) {
    NanScope();
    Local<Array> list = NanNew<Array>();
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
        Local<Object> obj = NanNew<Object>();
        PushProperties(obj, dev);
        obj->Set(NanNew<String>("syspath"), NanNew<String>(path));
        list->Set(i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    NanReturnValue(list);
}

static void Init(Handle<Object> target) {
    udev = udev_new();
    if (!udev) {
        NanThrowError("Can't create udev\n");
    }
    target->Set(
        NanNew<String>("list"),
        NanNew<FunctionTemplate>(List)->GetFunction());

    Monitor::Init(target);
}
NODE_MODULE(udev, Init)
