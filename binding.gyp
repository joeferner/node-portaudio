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
        "<!(node -e \"require('nan')\")", "portaudio/include"
      ],
      "conditions" : [
        [
          'OS=="mac"', {
            'xcode_settings': {
              'GCC_ENABLE_CPP_RTTI': 'YES',
              'MACOSX_DEPLOYMENT_TARGET': '10.7',
              'OTHER_CPLUSPLUSFLAGS': [
                '-std=c++11',
                '-stdlib=libc++',
                '-fexceptions'
              ],
              'OTHER_LDFLAGS': [
                "-Wl,-rpath,<@(module_root_dir)/build/Release"
              ]
            },
            "link_settings": {
              "libraries": [
                "<@(module_root_dir)/build/Release/libportaudio.dylib"
              ]
            },
            "copies": [
              {
                "destination": "build/Release/",
                "files": [
                  "<!@(ls -1 portaudio/bin/libportaudio.dylib)"
                ]
              }
            ]
          }
        ]
      ]
    }
  ]
}
