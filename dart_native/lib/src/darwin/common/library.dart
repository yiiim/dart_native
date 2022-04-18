import 'dart:ffi';

import 'package:dart_native/src/darwin/common/callback_manager.dart';
import 'package:dart_native/src/darwin/runtime/internal/nsobject_lifecycle.dart';

DynamicLibrary? _runtimeLib;
DynamicLibrary get runtimeLib {
  if (_runtimeLib != null) {
    return _runtimeLib!;
  }

  try {
    // Release mode
    _runtimeLib = DynamicLibrary.open('DartNative.framework/DartNative');
  } catch (e) {
    try {
      // Debug mode and use_frameworks!
      _runtimeLib = DynamicLibrary.open('dart_native.framework/dart_native');
    } catch (e) {
      // Debug mode
      _runtimeLib = nativeDylib;
    }
  }
  registerDeallocCallback(nativeObjectDeallocPtr.cast());
  return _runtimeLib!;
}

final DynamicLibrary nativeDylib = DynamicLibrary.process();

final initializeApi = runtimeLib.lookupFunction<IntPtr Function(Pointer<Void>),
    int Function(Pointer<Void>)>("InitDartApiDL");

final _dartAPIResult = initializeApi(NativeApi.initializeApiDLData);
final initDartAPISuccess = _dartAPIResult == 0;