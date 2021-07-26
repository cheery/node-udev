{
    "targets": [
        {
            "target_name": "udev",
            "sources": [ "udev.cc" ],
            "libraries": [
                "-ludev",
            ],
            "include_dirs" : [
                "<!@(node -p \"require('node-addon-api').include\")"
            ],
                    'dependencies': [
            "<!(node -p \"require('node-addon-api').gyp\")"
        ]
        }
    ]
}
