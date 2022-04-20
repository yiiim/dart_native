#include <unordered_map>
#include <list>
#include "dart_native.h"
#include "dn_thread.h"
#include "dn_log.h"
#include "dn_native_invoker.h"
#include "dn_callback.h"
#include "jni_object_ref.h"
#include "dn_jni_utils.h"
#include "dn_lifecycle_manager.h"

using namespace dartnative;

static JavaGlobalRef<jobject> *g_interface_registry = nullptr;
static jmethodID g_get_interface = nullptr;
static jmethodID g_get_signature = nullptr;
static std::unique_ptr<TaskRunner> g_task_runner = nullptr;

static std::unordered_map<std::string, DartThreadInfo> dart_interface_thread_cache;
static std::unordered_map<std::string, std::unordered_map<std::string, NativeMethodCallback>>
    dart_interface_method_cache;

void InitInterface() {
  auto env = AttachCurrentThread();
  auto registryClz =
      FindClass("com/dartnative/dart_native/InterfaceRegistry", env);
  auto instanceID =
      env->GetStaticMethodID(registryClz.Object(),
                             "getInstance",
                             "()Lcom/dartnative/dart_native/InterfaceRegistry;");
  JavaGlobalRef<jobject>
      registryObj(env->CallStaticObjectMethod(registryClz.Object(), instanceID), env);
  g_interface_registry = new JavaGlobalRef<jobject>(registryObj.Object(), env);
  g_get_interface = env->GetMethodID(registryClz.Object(), "getInterface",
                                     "(Ljava/lang/String;)Ljava/lang/Object;");
  g_get_signature = env->GetMethodID(registryClz.Object(), "getMethodsSignature",
                                     "(Ljava/lang/String;)Ljava/lang/String;");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *pjvm, void *reserved) {
  /// Init Java VM.
  InitWithJavaVM(pjvm);

  /// Init used class.
  InitClazz();

  /// Init task runner.
  g_task_runner = std::make_unique<TaskRunner>();

  /// Init interface.
  InitInterface();

  return JNI_VERSION_1_6;
}

intptr_t InitDartApiDL(void *data) {
  return Dart_InitializeApiDL(data);
}

/// release native object from cache
static void RunFinalizer(void *isolate_callback_data,
                         void *peer) {
  ReleaseJObject(static_cast<jobject>(peer));
}

void PassObjectToCUseDynamicLinking(Dart_Handle h, void *objPtr) {
  if (Dart_IsError_DL(h)) {
    DNError("Dart_IsError_DL");
    return;
  }
  RetainJObject(static_cast<jobject>(objPtr));
  intptr_t size = 8;
  Dart_NewWeakPersistentHandle_DL(h, objPtr, size, RunFinalizer);
}

void *GetClassName(void *objectPtr) {
  if (objectPtr == nullptr) {
    return nullptr;
  }
  auto env = AttachCurrentThread();
  auto cls = FindClass("java/lang/Class", env);
  jmethodID getName = env->GetMethodID(cls.Object(), "getName", "()Ljava/lang/String;");
  auto object = static_cast<jobject>(objectPtr);
  JavaLocalRef<jclass> objCls(env->GetObjectClass(object), env);
  JavaLocalRef<jstring> jstr((jstring) env->CallObjectMethod(objCls.Object(), getName), env);
  uint16_t *clsName = JavaStringToDartString(env, jstr.Object());

  return clsName;
}

void *CreateTargetObject(char *targetClassName,
                         void **arguments,
                         char **argumentTypes,
                         int argumentCount,
                         uint32_t stringTypeBitmask) {
  JNIEnv *env = AttachCurrentThread();
  auto cls = FindClass(targetClassName, env);
  auto newObj = NewObject(cls.Object(), arguments, argumentTypes, argumentCount, stringTypeBitmask);
  jobject gObj = env->NewGlobalRef(newObj.Object());
  return gObj;
}

void *InvokeNativeMethod(void *objPtr,
                         char *methodName,
                         void **arguments,
                         char **dataTypes,
                         int argumentCount,
                         char *returnType,
                         uint32_t stringTypeBitmask,
                         void *callback,
                         Dart_Port dartPort,
                         int thread,
                         bool isInterface) {
  auto object = static_cast<jobject>(objPtr);
  /// interface skip object check
  if (!isInterface && !ObjectInReference(object)) {
    /// maybe use cache pointer but jobject is release
    DNError(
        "InvokeNativeMethod not find class, check pointer and jobject lifecycle is same");
    return nullptr;
  }
  auto type = TaskThread(thread);
  auto invokeFunction = [=] {
    return DoInvokeNativeMethod(object, methodName, arguments, dataTypes, argumentCount, returnType,
                                stringTypeBitmask, callback, dartPort, type);
  };
  if (type == TaskThread::kFlutterUI) {
    return invokeFunction();
  }

  if (g_task_runner == nullptr) {
    DNError("InvokeNativeMethod error");
    return nullptr;
  }

  g_task_runner->ScheduleInvokeTask(type, invokeFunction);
  return nullptr;
}

/// register listener object by using java dynamic proxy
void RegisterNativeCallback(void *dartObject,
                            char *clsName,
                            char *funName,
                            void *callback,
                            Dart_Port dartPort) {
  JNIEnv *env = AttachCurrentThread();
  auto callbackManager =
      FindClass("com/dartnative/dart_native/CallbackManager", env);
  jmethodID registerCallback = env->GetStaticMethodID(callbackManager.Object(),
                                                      "registerCallback",
                                                      "(JLjava/lang/String;)Ljava/lang/Object;");

  auto dartObjectAddress = (jlong) dartObject;
  /// create interface object using java dynamic proxy
  JavaLocalRef<jstring> newClsName(env->NewStringUTF(clsName), env);
  JavaLocalRef<jobject> proxyObject(env->CallStaticObjectMethod(callbackManager.Object(),
                                                                registerCallback,
                                                                dartObjectAddress,
                                                                newClsName.Object()), env);
  jobject gProxyObj = env->NewGlobalRef(proxyObject.Object());

  /// save object into cache
  doRegisterNativeCallback(dartObject, gProxyObj, funName, callback, dartPort);
}

/// dart notify run callback function
void ExecuteCallback(WorkFunction *work_ptr) {
  const WorkFunction work = *work_ptr;
  work();
  delete work_ptr;
}

void *InterfaceHostObjectWithName(char *name) {
  auto env = AttachCurrentThread();
  if (env == nullptr) {
    return nullptr;
  }

  if (g_interface_registry == nullptr || g_get_interface == nullptr) {
    return nullptr;
  }

  JavaLocalRef<jstring> interfaceName(env->NewStringUTF(name), env);
  if (interfaceName.IsNull()) {
    return nullptr;
  }

  auto interface =
      env->CallObjectMethod(g_interface_registry->Object(), g_get_interface, interfaceName.Object());
  return interface;
}

void *InterfaceAllMetaData(char *name) {
  auto env = AttachCurrentThread();
  if (env == nullptr) {
    return nullptr;
  }

  if (g_interface_registry == nullptr || g_get_signature == nullptr) {
    return nullptr;
  }

  JavaLocalRef<jstring> interfaceName(env->NewStringUTF(name), env);
  if (interfaceName.IsNull()) {
    return nullptr;
  }

  JavaLocalRef<jstring> signatures((jstring) env->CallObjectMethod(g_interface_registry->Object(),
                                                                   g_get_signature,
                                                                   interfaceName.Object()), env);
  return JavaStringToDartString(env, signatures.Object());
}

void InterfaceRegisterDartInterface(char *interface, char *method,
                                    void *callback, Dart_Port dartPort) {
  auto interface_str = std::string(interface);
  auto method_cache = dart_interface_method_cache[interface_str];
  method_cache[std::string(method)] = (NativeMethodCallback) callback;
  dart_interface_method_cache[interface_str] = method_cache;
  dart_interface_thread_cache[interface_str] = {dartPort, std::this_thread::get_id()};
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_dartnative_dart_1native_DartNativeInterface_nativeInvokeMethod(JNIEnv *env,
                                                                        jobject thiz,
                                                                        jstring interface_name,
                                                                        jstring method,
                                                                        jobjectArray arguments,
                                                                        jobjectArray argument_types,
                                                                        jint argument_count) {
  const char *interface_char = env->GetStringUTFChars(interface_name, NULL);
  const char *method_char = env->GetStringUTFChars(method, NULL);
  auto method_map = dart_interface_method_cache[std::string(interface_char)];
  auto dart_function = method_map[std::string(method_char)];
  if (dart_function == nullptr) {
    return nullptr;
  }

  char **type_array = new char *[argument_count + 1];
  void **argument_array = new void *[argument_count + 1];

  /// store argument to pointer
  for (int i = 0; i < argument_count; ++i) {
    JavaLocalRef<jstring>
        argTypeString((jstring) env->GetObjectArrayElement(argument_types, i), env);
    JavaLocalRef<jobject> argument(env->GetObjectArrayElement(arguments, i), env);
    type_array[i] = (char *) env->GetStringUTFChars(argTypeString.Object(), NULL);
    if (strcmp(type_array[i], "java.lang.String") == 0) {
      /// argument will delete in JavaStringToDartString
      argument_array[i] = JavaStringToDartString(env, (jstring) argument.Object());
    } else {
      jobject gObj = env->NewGlobalRef(argument.Object());
      argument_array[i] = gObj;
    }
  }

  char *return_type = (char *) "java.lang.Object";
  /// the last pointer is return type
  type_array[argument_count] = return_type;

  auto thread_info = dart_interface_thread_cache[interface_char];

  jobject callbackResult =
      InvokeDartFunction(thread_info.thread_id == std::this_thread::get_id(),
                         dart_function,
                         (void *)interface_char,
                         (char *)method_char,
                         argument_array,
                         type_array,
                         argument_count,
                         return_type,
                         thread_info.dart_port,
                         env);

  if (method_char != nullptr) {
    env->ReleaseStringUTFChars(method, method_char);
  }
  if (interface_char != nullptr) {
    env->ReleaseStringUTFChars(interface_name, interface_char);
  }
  delete[] argument_array;
  delete[] type_array;

  return callbackResult;
}