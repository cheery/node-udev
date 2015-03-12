{
    "targets": [
        {
            "target_name": "udev",
            "sources": [ "udev.cc" ],
            "libraries": [
                "-ludev",
            ],
            "include_dirs" : [
                "<!(node -e \"require('nan')\")"
            ]
        }
    ]
}
