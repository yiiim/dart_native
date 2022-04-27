#include "dart_native.h"
#include "dn_thread.h"
#include "dn_log.h"
#include "dn_native_invoker.h"
#include "dn_callback.h"
#include "jni_object_ref.h"
#include "dn_jni_utils.h"
#include "dn_lifecycle_manager.h"
#include "dn_interface.h"

using namespace dartnative;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *pjvm, void *reserved) {
  /// Init Java VM.
  InitWithJavaVM(pjvm);

  auto env = AttachCurrentThread();
  if (env == nullptr) {
    DNError("JNI_OnLoad error, no JNIEnv provided!");
    return JNI_VERSION_1_6;
  }

  // Init used class.
  InitClazz(env);

  // Init task runner.
  InitTaskRunner();

  // Init interface.
  InitInterface(env);

  // Init callback.
  InitCallback(env);

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
    DNError("PassObjectToCUseDynamicLinking error!");
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
  if (env == nullptr) {
    DNError("GetClassName error, no JNIEnv provided!");
    return nullptr;
  }

  auto cls = FindClass("java/lang/Class", env);
  if (cls.IsNull()) {
    return nullptr;
  }

  jmethodID getName = env->GetMethodID(cls.Object(), "getName", "()Ljava/lang/String;");
  if (getName == nullptr) {
    DNError("GetClassName error, could not locate getName method!");
    return nullptr;
  }

  auto object = static_cast<jobject>(objectPtr);
  JavaLocalRef<jclass> objCls(env->GetObjectClass(object), env);
  JavaLocalRef<jstring> jstr((jstring) env->CallObjectMethod(objCls.Object(), getName), env);
  if (ClearException(env)) {
    DNError("GetClassName error, invoke get class name error!");
    return nullptr;
  }
  uint16_t *clsName = JavaStringToDartString(env, jstr.Object());
  return clsName;
}

void *CreateTargetObject(char *targetClassName,
                         void **arguments,
                         char **argumentTypes,
                         int argumentCount,
                         uint32_t stringTypeBitmask) {
  JNIEnv *env = AttachCurrentThread();
  if (env == nullptr) {
    DNError("CreateTargetObject error, no JNIEnv provided!");
    return nullptr;
  }

  auto cls = FindClass(targetClassName, env);
  if (cls.IsNull()) {
    return nullptr;
  }

  auto newObj = NewObject(cls.Object(), arguments, argumentTypes, argumentCount, stringTypeBitmask);
  if (newObj.IsNull()) {
    DNError("CreateTargetObject error, new object get null!");
    return nullptr;
  }

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
    DNError("InvokeNativeMethod not find class, check pointer and jobject lifecycle is same");
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

  ScheduleInvokeTask(type, invokeFunction);
  return nullptr;
}

/// register listener object by using java dynamic proxy
void RegisterNativeCallback(void *dartObject,
                            char *clsName,
                            char *funName,
                            void *callback,
                            Dart_Port dartPort) {
  JNIEnv *env = AttachCurrentThread();
  if (env == nullptr) {
    DNError("RegisterNativeCallback error, no JNIEnv provided!");
    return;
  }
  DoRegisterNativeCallback(dartObject, clsName, funName, callback, dartPort, env);
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
    DNError("InterfaceHostObjectWithName error, no JNIEnv provided!");
    return nullptr;
  }

  return InterfaceWithName(name, env);
}

void *InterfaceAllMetaData(char *name) {
  auto env = AttachCurrentThread();
  if (env == nullptr) {
    DNError("InterfaceAllMetaData error, no JNIEnv provided!");
    return nullptr;
  }

  return InterfaceMetaData(name, env);
}

void InterfaceRegisterDartInterface(char *interface, char *method,
                                    void *callback, Dart_Port dartPort, int32_t return_async) {
  RegisterDartInterface(interface, method, callback, dartPort, return_async);
}
