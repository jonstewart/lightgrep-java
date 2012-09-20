#include "lightgrep_c_api.h"
#include "jlightgrep.h"

#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>

static const char* ALL_IS_LOST = "Fuck it, Dude. Let's go bowling.";

static const char* programHandleClassName = "com/lightboxtechnologies/lightgrep/ProgramHandle";
static const char* contextHandleClassName = "com/lightboxtechnologies/lightgrep/ContextHandle";

static const char* hitCallbackClassName = "com/lightboxtechnologies/lightgrep/HitCallback";
static const char* searchHitClassName = "com/lightboxtechnologies/lightgrep/SearchHit";

static const char* nullPointerExceptionClassName = "java/lang/NullPointerException";
static const char* illegalStateExceptionClassName = "java/lang/IllegalStateException";
static const char* indexOutOfBoundsExceptionClassName = "java/lang/IndexOutOfBoundsException";
static const char* keywordExceptionClassName = "com/lightboxtechnologies/lightgrep/KeywordException";
static const char* programExceptionClassName = "com/lightboxtechnologies/lightgrep/ProgramException";

class PendingException {};

void throwIfException(JNIEnv* env) {
  if (env->ExceptionCheck()) {
    throw PendingException();
  }
}

static void throwException(JNIEnv* env, const char* exClassName, const char* message) {
  jclass ex = env->FindClass(exClassName);
  if (!env->ExceptionCheck()) {
    if (env->ThrowNew(ex, message)) {
      env->FatalError(ALL_IS_LOST);
    }
  }

  throw PendingException();
}

template <typename V>
static void throwIfNull(JNIEnv* env, const char* varname, const V* var) {
  if (var == nullptr) {
    std::ostringstream ss;
    ss << varname << " == null";

    throwException(env, nullPointerExceptionClassName, ss.str().c_str());
  }
}

template <typename V>
static void throwIfNegative(JNIEnv* env, const char* varname, V var) {
  if (var < 0) {
    std::ostringstream ss;
    ss << varname << " == " << var << " < 0";

    throwException(env, indexOutOfBoundsExceptionClassName, ss.str().c_str());
  }
}

template <typename O, typename S>
static void throwIfBufferTooSmall(JNIEnv* env, const char* bufname, jbyteArray buffer, const char* offname, O offset, const char* sname, S size) {
  const jsize buflen = env->GetArrayLength(buffer);
  if (buflen - offset < size) {
    std::ostringstream ss;
    ss << bufname << ".length == " << buflen << ", "
       << offname << " == " << offset << ", "
       << bufname << ".length - " << offname << " < "
       << sname << " == " << size;

    throwException(env, indexOutOfBoundsExceptionClassName, ss.str().c_str());
  }
}

template <typename V>
static void throwIfDestroyed(JNIEnv* env, const V* ptr) {
  if (!ptr) {
    throwException(
      env,
      illegalStateExceptionClassName,
      "Tried calling method on destroyed handle"
    );
  }
}

//
// We cache field and method IDs in static init() methods for each class,
// in order to ensure that they get reset if a class is reloaded.
//

static jfieldID parserHandlePointerField;

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ParserHandle_init(JNIEnv* env, jclass cl) {
  try {
    parserHandlePointerField = env->GetFieldID(cl, "Pointer", "J");
    throwIfException(env);
  }
  catch (const PendingException&) {
  }
}

static jfieldID programHandlePointerField;
static jmethodID programHandleCtor;

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_init(JNIEnv* env, jclass cl) {
  try {
    programHandleCtor = env->GetMethodID(cl, "<init>", "(J)V");
    throwIfException(env);

    programHandlePointerField = env->GetFieldID(cl, "Pointer", "J");
    throwIfException(env);
  }
  catch (const PendingException&) {
  }
}

static jfieldID contextHandlePointerField;
static jmethodID contextHandleCtor;

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_init(JNIEnv* env, jclass cl) {
  try {
    contextHandleCtor = env->GetMethodID(cl, "<init>", "(J)V");
    throwIfException(env);

    contextHandlePointerField = env->GetFieldID(cl, "Pointer", "J");
    throwIfException(env);
  }
  catch (const PendingException&) {
  }
}

static jfieldID keyOptionsFixedStringField;
static jfieldID keyOptionsCaseInsensitiveField;

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_KeyOptions_init(JNIEnv* env, jclass cl) {
  try {
    keyOptionsFixedStringField = env->GetFieldID(cl, "FixedString", "Z");
    throwIfException(env); 

    keyOptionsCaseInsensitiveField = env->GetFieldID(cl, "FixedString", "Z");
    throwIfException(env); 
  }
  catch (const PendingException&) {
  }
}

static jfieldID programOptionsDeterminizeField;

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramOptions_init(JNIEnv* env, jclass cl) {
  try {
    programOptionsDeterminizeField = env->GetFieldID(cl, "Determinize", "Z");
    throwIfException(env); 
  }
  catch (const PendingException&) {
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextOptions_init(JNIEnv*, jclass) {
  try {
  }
  catch (const PendingException&) {
  } 
}

static jclass hitCallbackClass;
static jmethodID hitCallbackCallback;

static jclass searchHitClass;
static jmethodID searchHitCtor;
static jfieldID searchHitStartField;
static jfieldID searchHitEndField;
static jfieldID searchHitKeywordIndexField;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* jvm, void*) {
  try {
    JNIEnv* env;
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
      throw PendingException();
    }

    // We make a global reference for HitCallback because it's not possible
    // to put a static block or any method implementations into an interface,
    // which prevents us from reseting the callback() method ID in the event
    // that HitCallback is unloaded and reloaded. The global reference blocks
    // this by preventing HitCallback from being unloaded.
    jclass cl = env->FindClass(hitCallbackClassName);
    throwIfException(env);

    hitCallbackClass = reinterpret_cast<jclass>(env->NewGlobalRef(cl));
    if (!hitCallbackClass) throw PendingException();

    hitCallbackCallback = env->GetMethodID(hitCallbackClass, "callback", "(Lcom/lightboxtechnologies/lightgrep/SearchHit;)V");
    throwIfException(env);

    // We make a global reference for SearchHit to avoid calling FindClass
    // for it on every hit in the callbackShim.
    cl = env->FindClass(searchHitClassName);
    throwIfException(env);

    searchHitClass = reinterpret_cast<jclass>(env->NewGlobalRef(cl));
    throwIfException(env);

    searchHitCtor = env->GetMethodID(searchHitClass, "<init>", "(JJI)V");
    throwIfException(env);

    searchHitStartField = env->GetFieldID(searchHitClass, "Start", "J");
    throwIfException(env);

    searchHitEndField = env->GetFieldID(searchHitClass, "End", "J");
    throwIfException(env);

    searchHitKeywordIndexField = env->GetFieldID(searchHitClass, "KeywordIndex", "I");
    throwIfException(env);

    return JNI_VERSION_1_6;
  }
  catch (const PendingException&) {
    return JNI_ERR;
  }
}

JNIEXPORT void JNI_OnUnload(JavaVM* jvm, void*) {
  JNIEnv* env;
  if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    // Everything is hosed if this happens. We can't even throw an
    // exception back into Java, since to do that we'd need a valid JNIEnv.
    return;
  }

  env->DeleteGlobalRef(hitCallbackClass);
  env->DeleteGlobalRef(searchHitClass);
}

JNIEXPORT jlong JNICALL Java_com_lightboxtechnologies_lightgrep_ParserHandle_create(JNIEnv*, jclass, jint numFsmStateSizeHint) {
  return reinterpret_cast<jlong>(lg_create_parser(numFsmStateSizeHint));
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ParserHandle_destroy(JNIEnv* env, jobject hParser) {
  LG_HPARSER ptr = reinterpret_cast<LG_HPARSER>(
    env->GetLongField(hParser, parserHandlePointerField)
  );
  if (ptr) {
    lg_destroy_parser(ptr);
    env->SetLongField(hParser, parserHandlePointerField, 0);
  }
}

JNIEXPORT jint JNICALL Java_com_lightboxtechnologies_lightgrep_ParserHandle_addKeyword(JNIEnv* env, jobject hParser, jstring keyword, jint keyIndex, jobject options, jstring encoding) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "keyword", keyword);
    throwIfNegative(env, "keyIndex", keyIndex);
    throwIfNull(env, "options", options);
    throwIfNull(env, "encoding", encoding);

    // convert all of the Java objects to C
    LG_HPARSER ptr = reinterpret_cast<LG_HPARSER>(
      env->GetLongField(hParser, parserHandlePointerField)
    );
    throwIfDestroyed(env, ptr); 

    using namespace std::placeholders;

    std::unique_ptr<const char,std::function<void(const char*)>> kw(
      env->GetStringUTFChars(keyword, nullptr),
      std::bind(&JNIEnv::ReleaseStringUTFChars, env, keyword, _1)
    );

    if (!kw) {
      // OutOfMemoryError already thrown
      throw PendingException();
    }

    LG_KeyOptions opts{
      env->GetBooleanField(options, keyOptionsFixedStringField) != 0,
      env->GetBooleanField(options, keyOptionsCaseInsensitiveField) != 0
    };

    std::unique_ptr<const char,std::function<void(const char*)>> enc(
      env->GetStringUTFChars(encoding, nullptr),
      std::bind(&JNIEnv::ReleaseStringUTFChars, env, encoding, _1)
    );

    if (!enc) {
      // OutOfMemoryError already thrown
      throw PendingException(); 
    }

    // finally actually do something
    const int ret = lg_add_keyword(
      ptr,
      kw.get(),
      std::strlen(kw.get()),
      (uint32) keyIndex,
      &opts,
      enc.get()
    );

    if (!ret) {
      throwException(env, keywordExceptionClassName, lg_error(ptr));
    }

    return ret;
  }
  catch (const PendingException&) {
    return 0;
  }
}

static jobject makeProgramHandle(JNIEnv* env, LG_HPROGRAM hProg) {
  jclass cl = env->FindClass(programHandleClassName);
  throwIfException(env);

  jobject obj = env->NewObject(
    cl,
    programHandleCtor,
    reinterpret_cast<jlong>(hProg)
  );
  throwIfException(env);

  return obj;
}

JNIEXPORT jobject JNICALL Java_com_lightboxtechnologies_lightgrep_ParserHandle_createProgram(JNIEnv* env, jobject hParser, jobject options) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "options", options);
  
    // convert all of the Java objects to C
    LG_HPARSER ptr = reinterpret_cast<LG_HPARSER>(
      env->GetLongField(hParser, parserHandlePointerField)
    );
    throwIfDestroyed(env, ptr); 

    LG_ProgramOptions opts{
      env->GetBooleanField(options, programOptionsDeterminizeField) != 0
    };

    // finally actually do something
    LG_HPROGRAM hProg = lg_create_program(ptr, &opts);
    // FIXME: don't use lg_ok
    if (!lg_ok(hProg)) {
      throwException(env, keywordExceptionClassName, lg_error(hProg));
    }
    return makeProgramHandle(env, hProg);
  }
  catch (const PendingException&) {
    return nullptr;
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_destroy(JNIEnv* env, jobject hProg) {
  LG_HPROGRAM ptr = reinterpret_cast<LG_HPROGRAM>(
    env->GetLongField(hProg, programHandlePointerField)
  );
  if (ptr) {
    lg_destroy_program(ptr);
    env->SetLongField(hProg, programHandlePointerField, 0);
  }
}

JNIEXPORT jint JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_size(JNIEnv* env, jobject hProg) {
  try {
    LG_HPROGRAM ptr = reinterpret_cast<LG_HPROGRAM>(
      env->GetLongField(hProg, programHandlePointerField)
    );
    throwIfDestroyed(env, ptr);
    return lg_program_size(ptr);
  }
  catch (const PendingException&) {
    return -1;
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_write(JNIEnv* env, jobject hProg, jbyteArray buffer, jint offset) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "buffer", buffer);
    throwIfNegative(env, "offset", offset);

    LG_HPROGRAM ptr = reinterpret_cast<LG_HPROGRAM>(
      env->GetLongField(hProg, programHandlePointerField)
    );
    throwIfDestroyed(env, ptr);

    throwIfBufferTooSmall(
      env,
      "buffer", buffer,
      "offset", offset,
      "program size", lg_program_size(ptr)
    );

    // convert all of the Java objects to C
    using namespace std::placeholders;

    std::unique_ptr<void,std::function<void(void*)>> data(
      env->GetPrimitiveArrayCritical(buffer, nullptr),
      std::bind(&JNIEnv::ReleasePrimitiveArrayCritical, env, buffer, _1, 0)
    );

    if (!data) {
      // OutOfMemoryError already thrown
      throw PendingException();
    }

    char* buf = reinterpret_cast<char*>(data.get()) + (uint32) offset;

    // finally actually do something
    lg_write_program(ptr, buf);
  }
  catch (const PendingException&) {
  }
}

JNIEXPORT jobject JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_read(JNIEnv* env, jclass, jbyteArray buffer, jint offset, jint size) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "buffer", buffer);
    throwIfNegative(env, "offset", offset);
    throwIfNegative(env, "size", size);
    throwIfBufferTooSmall(
      env,
      "buffer", buffer,
      "offset", offset,
      "size", size
    );

    // convert all of the Java objects to C
    using namespace std::placeholders;

    std::unique_ptr<void,std::function<void(void*)>> data(
      env->GetPrimitiveArrayCritical(buffer, nullptr),
      std::bind(&JNIEnv::ReleasePrimitiveArrayCritical, env, buffer, _1, 0)
    );

    if (!data) {
      // OutOfMemoryError already thrown
      throw PendingException();
    }

    char* buf = reinterpret_cast<char*>(data.get()) + (uint32) offset;

    // finally actually do something
    LG_HPROGRAM hProg = lg_read_program(buf, (uint32) size);
    if (!lg_ok(hProg)) {
      throwException(env, programExceptionClassName, lg_error(hProg));
    }
    return makeProgramHandle(env, hProg);
  }
  catch (const PendingException&) {
    return nullptr;
  }
}

static jobject makeContextHandle(JNIEnv* env, LG_HCONTEXT hCtx) {
  jclass cl = env->FindClass(contextHandleClassName);
  throwIfException(env);
  
  jobject obj = env->NewObject(
    cl,
    contextHandleCtor,
    reinterpret_cast<jlong>(hCtx)
  );
  throwIfException(env);

  return obj;
}

JNIEXPORT jobject JNICALL Java_com_lightboxtechnologies_lightgrep_ProgramHandle_createContext(JNIEnv* env, jobject hProg, jobject options) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "options", options);
  
    // convert all of the Java objects to C
    LG_HPROGRAM ptr = reinterpret_cast<LG_HPROGRAM>(
      env->GetLongField(hProg, programHandlePointerField)
    );
    throwIfDestroyed(env, ptr);

    LG_ContextOptions opts{
      std::numeric_limits<uint64>::max(),
      std::numeric_limits<uint64>::max()
    };

    LG_HCONTEXT hCtx = lg_create_context(ptr, &opts);
    if (!lg_ok(hCtx)) {
      throwException(env, programExceptionClassName, lg_error(hCtx));
    }
    return makeContextHandle(env, hCtx);
  }
  catch (const PendingException&) {
    return nullptr;
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_destroy(JNIEnv* env, jobject hCtx) {
  LG_HCONTEXT ptr = reinterpret_cast<LG_HCONTEXT>(
     env->GetLongField(hCtx, contextHandlePointerField)
  );
  if (ptr) {
    lg_destroy_context(ptr);
    env->SetLongField(hCtx, contextHandlePointerField, 0);
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_reset(JNIEnv* env, jobject hCtx) {
  try {
    LG_HCONTEXT ptr = reinterpret_cast<LG_HCONTEXT>(
      env->GetLongField(hCtx, contextHandlePointerField)
    );
    throwIfDestroyed(env, ptr);
    lg_reset_context(ptr);
  }
  catch (const PendingException&) {
  }
}

static void callbackShim(void* userData, const LG_SearchHit* const hit) {
  // NB: Throwing C++ exceptions here is super wacky, as they pass into
  // and then back out of our C API, finally being caught in our wrappers
  // for lg_search or lg_starts_with. This works due to our having no
  // exception_trap in those two C API functions.

  JNIEnv* env;
  jobject cb;
  std::tie(env, cb) = *reinterpret_cast<std::tuple<JNIEnv*,jobject>*>(userData);

  jobject hobj = env->NewObject(
    searchHitClass,
    searchHitCtor,
    (jlong) hit->Start,
    (jlong) hit->End,
    (jint) hit->KeywordIndex
  );
  throwIfException(env);

  env->CallVoidMethod(cb, hitCallbackCallback, hobj);
  throwIfException(env);
}

JNIEXPORT jint JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_search(JNIEnv* env, jobject hCtx, jbyteArray buffer, jint offset, jint size, jlong startOffset, jobject callback) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "buffer", buffer);
    throwIfNegative(env, "offset", offset);
    throwIfNegative(env, "size", size);
    throwIfBufferTooSmall(
      env,
      "buffer", buffer,
      "offset", offset,
      "size", size
    );
    throwIfNegative(env, "startOffset", startOffset);
    throwIfNull(env, "callback", callback);

    // convert all of the Java objects to C
    LG_HCONTEXT ptr = reinterpret_cast<LG_HCONTEXT>(
      env->GetLongField(hCtx, contextHandlePointerField)
    );
    throwIfDestroyed(env, ptr);

    using namespace std::placeholders;

    std::unique_ptr<jbyte,std::function<void(jbyte*)>> data(
      env->GetByteArrayElements(buffer, nullptr),
      std::bind(&JNIEnv::ReleaseByteArrayElements, env, buffer, _1, JNI_ABORT)
    );

    if (!data) {
      // OutOfMemoryError already thrown
      throw PendingException();
    }
    
    const char* buf = reinterpret_cast<const char*>(data.get()) + offset;
 
    std::tuple<JNIEnv*,jobject> userData{env, callback};

    // finally actually do something
    return lg_search(
      ptr,
      buf,
      buf + size,
      (uint64) startOffset,
      &userData,
      callbackShim
    );
  }
  catch (const PendingException&) {
    return 0;
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_closeoutSearch(JNIEnv* env, jobject hCtx, jobject callback) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "callback", callback);

    // convert all of the Java objects to C
    LG_HCONTEXT ptr = reinterpret_cast<LG_HCONTEXT>(
      env->GetLongField(hCtx, contextHandlePointerField)
    );
    throwIfDestroyed(env, ptr);

    std::tuple<JNIEnv*,jobject> userData{env, callback};

    // finally actually do something
    lg_closeout_search(ptr, &userData, callbackShim);
  }
  catch (const PendingException&) {
  }
}

JNIEXPORT void JNICALL Java_com_lightboxtechnologies_lightgrep_ContextHandle_startsWith(JNIEnv* env, jobject hCtx, jbyteArray buffer, jint offset, jint size, jlong startOffset, jobject callback) {
  try {
    // validate all of the arguments so we don't crash the JVM
    throwIfNull(env, "buffer", buffer);
    throwIfNegative(env, "offset", offset);
    throwIfNegative(env, "size", size);
    throwIfBufferTooSmall(
      env,
      "buffer", buffer,
      "offset", offset,
      "size", size
    );
    throwIfNegative(env, "startOffset", startOffset);
    throwIfNull(env, "callback", callback);

    // convert all of the Java objects to C
    LG_HCONTEXT ptr = reinterpret_cast<LG_HCONTEXT>(
      env->GetLongField(hCtx, contextHandlePointerField)
    );
    throwIfDestroyed(env, ptr);

    using namespace std::placeholders;

    std::unique_ptr<void,std::function<void(void*)>> data(
      env->GetPrimitiveArrayCritical(buffer, nullptr),
      std::bind(&JNIEnv::ReleasePrimitiveArrayCritical, env, buffer, _1, JNI_ABORT)
    );

    if (!data) {
      // OutOfMemoryError already thrown
      throw PendingException();
    }

    const char* buf = reinterpret_cast<const char*>(data.get()) + offset;

    std::tuple<JNIEnv*,jobject> userData{env, callback};

    // finally actually do something
    lg_starts_with(
      ptr,
      buf,
      buf + size,
      (uint64) startOffset,
      &userData,
      callbackShim
    );
  }
  catch (const PendingException&) {
  }
}

/*
JNIEXPORT jstring JNICALL Java_com_lightboxtechnologies_lightgrep_Lightgrep_lg_1error(JNIEnv* env, jclass, jobject handle) {
  jlong ptr = env->GetLongField(handle, handleField); 
  return env->NewStringUTF(lg_error(reinterpret_cast<void*>(ptr)));
}
*/
