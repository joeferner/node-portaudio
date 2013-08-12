
#include <node.h>
#include <v8.h>
#include <node_buffer.h>
#include <cstring>

v8::Handle<v8::Value> Open(const v8::Arguments& args);
v8::Handle<v8::Value> GetDevices(const v8::Arguments& args);
