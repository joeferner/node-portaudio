
#include <nan.h>
#include "nodePortAudio.h"

using namespace v8;

// extern "C" {
//   void init (v8::Handle<v8::Object> target)
//   {
//     v8::HandleScope scope;
//     NODE_SET_METHOD(target, "open", Open);
//     NODE_SET_METHOD(target, "getDevices", GetDevices);
//   }
// }

NAN_MODULE_INIT(Init) {
  Nan::Set(target, Nan::New("open").ToLocalChecked(),
     Nan::GetFunction(Nan::New<FunctionTemplate>(Open)).ToLocalChecked());
  Nan::Set(target, Nan::New("getDevices").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(GetDevices)).ToLocalChecked());
}

NODE_MODULE(portAudio, Init);
