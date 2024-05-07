#include "LlamaContext.h"
#include "DisposeWorker.h"
#include "LlamaCompletionWorker.h"
#include "LoadSessionWorker.h"
#include "SaveSessionWorker.h"

void LlamaContext::Init(Napi::Env env, Napi::Object &exports) {
  Napi::Function func = DefineClass(
      env, "LlamaContext",
      {InstanceMethod<&LlamaContext::GetSystemInfo>(
           "getSystemInfo",
           static_cast<napi_property_attributes>(napi_enumerable)),
       InstanceMethod<&LlamaContext::Completion>(
           "completion",
           static_cast<napi_property_attributes>(napi_enumerable)),
       InstanceMethod<&LlamaContext::StopCompletion>(
           "stopCompletion",
           static_cast<napi_property_attributes>(napi_enumerable)),
       InstanceMethod<&LlamaContext::SaveSession>(
           "saveSession",
           static_cast<napi_property_attributes>(napi_enumerable)),
       InstanceMethod<&LlamaContext::LoadSession>(
           "loadSession",
           static_cast<napi_property_attributes>(napi_enumerable)),
       InstanceMethod<&LlamaContext::Release>(
           "release", static_cast<napi_property_attributes>(napi_enumerable))});
  Napi::FunctionReference *constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
#if NAPI_VERSION > 5
  env.SetInstanceData(constructor);
#endif
  exports.Set("LlamaContext", func);
}

// construct({ model, embedding, n_ctx, n_batch, n_threads, n_gpu_layers,
// use_mlock, use_mmap }): LlamaContext throws error
LlamaContext::LlamaContext(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<LlamaContext>(info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Object expected").ThrowAsJavaScriptException();
  }
  auto options = info[0].As<Napi::Object>();

  gpt_params params;
  params.model = get_option<std::string>(options, "model", "");
  if (params.model.empty()) {
    Napi::TypeError::New(env, "Model is required").ThrowAsJavaScriptException();
  }
  params.embedding = get_option<bool>(options, "embedding", false);
  params.n_ctx = get_option<int32_t>(options, "n_ctx", 512);
  params.n_batch = get_option<int32_t>(options, "n_batch", 2048);
  params.n_threads =
      get_option<int32_t>(options, "n_threads", get_math_cpu_count() / 2);
  params.n_gpu_layers = get_option<int32_t>(options, "n_gpu_layers", -1);
  params.use_mlock = get_option<bool>(options, "use_mlock", false);
  params.use_mmap = get_option<bool>(options, "use_mmap", true);
  params.numa =
      static_cast<ggml_numa_strategy>(get_option<uint32_t>(options, "numa", 0));
  params.main_gpu = get_option<int32_t>(options, "main_gpu", 0);

  llama_backend_init();
  llama_numa_init(params.numa);

  llama_model *model;
  llama_context *ctx;
  std::tie(model, ctx) = llama_init_from_gpt_params(params);

  if (model == nullptr || ctx == nullptr) {
    Napi::TypeError::New(env, "Failed to load model")
        .ThrowAsJavaScriptException();
  }

  _sess = std::make_shared<LlamaSession>(ctx, params);
  _info = get_system_info(params);
}

// getSystemInfo(): string
Napi::Value LlamaContext::GetSystemInfo(const Napi::CallbackInfo &info) {
  return Napi::String::New(info.Env(), _info);
}

// completion(options: LlamaCompletionOptions, onToken?: (token: string) =>
// void): Promise<LlamaCompletionResult>
Napi::Value LlamaContext::Completion(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Object expected").ThrowAsJavaScriptException();
  }
  if (info.Length() >= 2 && !info[1].IsFunction()) {
    Napi::TypeError::New(env, "Function expected").ThrowAsJavaScriptException();
  }
  if (_sess == nullptr) {
    Napi::TypeError::New(env, "Context is disposed")
        .ThrowAsJavaScriptException();
  }
  auto options = info[0].As<Napi::Object>();

  gpt_params params = _sess->params();
  params.prompt = get_option<std::string>(options, "prompt", "");
  if (params.prompt.empty()) {
    Napi::TypeError::New(env, "Prompt is required")
        .ThrowAsJavaScriptException();
  }
  params.n_predict = get_option<int32_t>(options, "n_predict", -1);
  params.sparams.temp = get_option<float>(options, "temperature", 0.80f);
  params.sparams.top_k = get_option<int32_t>(options, "top_k", 40);
  params.sparams.top_p = get_option<float>(options, "top_p", 0.95f);
  params.sparams.min_p = get_option<float>(options, "min_p", 0.05f);
  params.sparams.tfs_z = get_option<float>(options, "tfs_z", 1.00f);
  params.sparams.mirostat = get_option<int32_t>(options, "mirostat", 0.00f);
  params.sparams.mirostat_tau =
      get_option<float>(options, "mirostat_tau", 5.00f);
  params.sparams.mirostat_eta =
      get_option<float>(options, "mirostat_eta", 0.10f);
  params.sparams.penalty_last_n =
      get_option<int32_t>(options, "penalty_last_n", 64);
  params.sparams.penalty_repeat =
      get_option<float>(options, "penalty_repeat", 1.00f);
  params.sparams.penalty_freq =
      get_option<float>(options, "penalty_freq", 0.00f);
  params.sparams.penalty_present =
      get_option<float>(options, "penalty_present", 0.00f);
  params.sparams.penalize_nl = get_option<bool>(options, "penalize_nl", false);
  params.sparams.typical_p = get_option<float>(options, "typical_p", 1.00f);
  params.ignore_eos = get_option<float>(options, "ignore_eos", false);
  params.sparams.grammar = get_option<std::string>(options, "grammar", "");
  params.n_keep = get_option<int32_t>(options, "n_keep", 0);
  params.seed = get_option<int32_t>(options, "seed", LLAMA_DEFAULT_SEED);
  std::vector<std::string> stop_words;
  if (options.Has("stop") && options.Get("stop").IsArray()) {
    auto stop_words_array = options.Get("stop").As<Napi::Array>();
    for (size_t i = 0; i < stop_words_array.Length(); i++) {
      stop_words.push_back(stop_words_array.Get(i).ToString().Utf8Value());
    }
  }

  Napi::Function callback;
  if (info.Length() >= 2) {
    callback = info[1].As<Napi::Function>();
  }

  auto *worker =
      new LlamaCompletionWorker(info, _sess, callback, params, stop_words);
  worker->Queue();
  _wip = worker;
  return worker->Promise();
}

// stopCompletion(): void
void LlamaContext::StopCompletion(const Napi::CallbackInfo &info) {
  if (_wip != nullptr) {
    _wip->Stop();
  }
}

// saveSession(path: string): Promise<void> throws error
Napi::Value LlamaContext::SaveSession(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
  }
  if (_sess == nullptr) {
    Napi::TypeError::New(env, "Context is disposed")
        .ThrowAsJavaScriptException();
  }
  auto *worker = new SaveSessionWorker(info, _sess);
  worker->Queue();
  return worker->Promise();
}

// loadSession(path: string): Promise<{ count }> throws error
Napi::Value LlamaContext::LoadSession(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
  }
  if (_sess == nullptr) {
    Napi::TypeError::New(env, "Context is disposed")
        .ThrowAsJavaScriptException();
  }
  auto *worker = new LoadSessionWorker(info, _sess);
  worker->Queue();
  return worker->Promise();
}

// release(): Promise<void>
Napi::Value LlamaContext::Release(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  if (_wip != nullptr) {
    _wip->Stop();
  }
  if (_sess == nullptr) {
    auto promise = Napi::Promise::Deferred(env);
    promise.Resolve(env.Undefined());
    return promise.Promise();
  }
  auto *worker = new DisposeWorker(info, std::move(_sess));
  worker->Queue();
  return worker->Promise();
}
