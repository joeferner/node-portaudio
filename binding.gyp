{
  "targets": [
    {
      "target_name": "naudiodon",
      "sources": [
        "src/naudiodon.cc",
        "src/GetDevices.cc",
        "src/AudioOutput.cc"
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ],
      "conditions" : [
        [
          'OS!="win"', {
            "libraries" : [
              '-lportaudio',
            ],
          }
        ],
        [
          'OS=="win"', {
            "libraries" : [
              '<(module_root_dir)/gyp/lib/libpa.dll.a'
            ]
          }
        ]
      ]
    }
  ]
}
