import 'dart:ffi';

import 'package:dart_native/dart_native.dart';

/// The mappings of dart class name and [Convertor].
Map<String, Convertor> _dataConvertorCache = {};

/// The mappings of java class name and [ConvertorToDartFromPointer].
Map<String, ConvertorToDartFromPointer> _javaConvertorCache = {};

/// Register native class name
/// and register a function for converting a Dart object from a [Pointer].
///
/// Example for [List]:
///
/// ```dart
/// registerJavaTypeConvertor('JList', 'java/util/List', (ptr) {
///     return JList.fromPointer(ptr);
/// });
/// ```
/// The example above can be generated by applying `@nativeWithClass` annotation on Dart
/// wrapper class.
///
/// dart class and java class are one-to-one mapping.
void registerJavaTypeConvertor(
    String dartClass, String javaClass, ConvertorToDartFromPointer convertor) {
  registerDartConvertor(dartClass, javaClass, convertor);
  registerJavaConvertor(javaClass, convertor);
}

/// See [_dataConvertorCache] definition.
void registerDartConvertor(
    String dartClass, String javaClass, ConvertorToDartFromPointer convertor) {
  if (_dataConvertorCache[dartClass] == null) {
    _dataConvertorCache[dartClass] = Convertor(javaClass, convertor);
  } else {
    throw 'registerDartConvertor has contains dartClass $dartClass.'
        ' Java class and dart class are one-to-one mapping.';
  }
}

/// See [_javaConvertorCache] definition.
void registerJavaConvertor(
    String javaClass, ConvertorToDartFromPointer convertor) {
  if (_javaConvertorCache[javaClass] == null) {
    _javaConvertorCache[javaClass] = convertor;
  } else {
    throw 'registerJavaConvertor has contains javaClass $javaClass.'
        ' Java class and dart class are one-to-one mapping.';
  }
}

/// Get register pointer convert.
/// if [_convertorCache] not contain this dartClass will return null.
ConvertorToDartFromPointer? getRegisterPointerConvertor(String dartClass) {
  return _dataConvertorCache[dartClass]?.convertor;
}

/// Get register java class name.
/// if [_convertorCache] not contain this dartClass will return null.
String? getRegisterJavaClass(String dartClass) {
  return _dataConvertorCache[dartClass]?.javaClass;
}

String? getRegisterJavaClassSignature(String dartClass) {
  String? cls = _dataConvertorCache[dartClass]?.javaClass;
  if (cls != null) {
    cls = 'L$cls;';
  }
  return cls;
}

/// convertor info
/// [javaClass] java class name, like java/lang/String
/// [convertor] java object pointer to dart object function
class Convertor {
  final String? javaClass;
  final ConvertorToDartFromPointer convertor;
  Convertor(this.javaClass, this.convertor);
}

/// Convert pointer to jobject.
///
/// must specify the java class name with [javaClass].
dynamic jobjectInstanceFromPointer(String javaClass, dynamic arg) {
  Pointer<Void> ptr;
  if (arg is JObject) {
    ptr = arg.pointer;
  } else if (arg is Pointer) {
    ptr = arg.cast<Void>();
  } else {
    return arg;
  }

  if (ptr == nullptr) {
    return arg;
  }
  ConvertorToDartFromPointer? convertor = _javaConvertorCache[javaClass];
  if (convertor != null) {
    return convertor(ptr);
  }
  return JObject.fromPointer(ptr, className: javaClass);
}
