{
  "targets": [
    {
      "target_name": "portAudio",
      "sources": [
        "src/binding.cpp",
        "src/nodePortAudio.cpp"
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
