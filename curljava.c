/*****************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 2001 - 2008 Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * In order to be useful for every potential user, curl and libcurl are
 * dual-licensed under the MPL and the MIT/X-derivate licenses.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the MPL or the MIT/X-derivate
 * licenses. You may pick one of these licenses.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * $Id: curljava.c 42 2008-10-20 09:27:21Z patrick $
 *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <curl/curl.h> /* libcurl header */
#include "CurlGlue.h"  /* the JNI-generated glue header file */

/*
 * This is a private struct allocated for every 'CurlGlue' object.
 */
typedef struct {
    jmethodID mid;
    JNIEnv *env;
    jclass cls; /* global reference */
    jobject object;
} jcallback_t;

typedef struct {
  CURL *curl;
  jcallback_t write;
  jcallback_t read;
} curljava_t;



/*
 * CurlGlue.version()
 */

JNIEXPORT jstring JNICALL Java_se_haxx_curl_CurlGlue_version(JNIEnv *env, jclass myclass)

{
  return (*env)->NewStringUTF(env, curl_version());
}

JNIEXPORT jlong JNICALL Java_se_haxx_curl_CurlGlue_jni_1init(JNIEnv *env,
                                               jobject myself)
{
  curljava_t *jcurl = NULL;

  CURL *curl = curl_easy_init();
  if (!curl) {
    return (long)jcurl;
  }
  
  jcurl = (curljava_t *) malloc(sizeof(curljava_t));

  if (jcurl) {
    memset((void *)jcurl, 0, sizeof(curljava_t));
    jcurl->curl = curl;
  } else {
    curl_easy_cleanup(curl);
  }

  return (long)jcurl;
}

JNIEXPORT void JNICALL Java_se_haxx_curl_CurlGlue_jni_1cleanup(JNIEnv *env,
                                                  jobject myself,
                                                  jlong handle)
{
  curljava_t *jcurl = (curljava_t *)handle;

  if(jcurl->write.cls) {
    /* a global reference we must delete */
    (*env)->DeleteGlobalRef(env, jcurl->write.cls);
    (*env)->DeleteGlobalRef(env, jcurl->write.object);
  }

  if(jcurl->read.cls) {
    /* a global reference we must delete */
    (*env)->DeleteGlobalRef(env, jcurl->read.cls);
    (*env)->DeleteGlobalRef(env, jcurl->read.object);
  }

  curl_easy_cleanup(jcurl->curl); /* cleanup libcurl stuff */

  free(jcurl); /* free the struct too */
}

/*
 * setopt() int + string
 */
JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1setopt__JILjava_lang_String_2
  (JNIEnv *env, jobject myself, jlong handle, jint option, jstring value)
{
  curljava_t *jcurl = (curljava_t *)handle;

  /* get the actual string C-style */
  const char *str = (char *)(*env)->GetStringUTFChars(env, value, NULL);

#ifdef DEBUG
  puts("setopt int + string");
#endif

  return curl_easy_setopt(jcurl->curl, (CURLoption)option, str);
}

/*
 * setopt() int + int
 */
JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1setopt__JII
  (JNIEnv *env, jobject myself, jlong handle, jint option, jint value)
{
  curljava_t *jcurl = (curljava_t *)handle;
  CURLoption opt = (CURLoption)((int)option);

#ifdef DEBUG
  puts("setopt int + int");
#endif

  switch(opt) {
  case CURLOPT_WRITEDATA:
  case CURLOPT_READDATA:
    /* silently ignored, we don't need user-specified callback data when
       we have an object, and besides the CURLOPT_WRITEDATA and CURLOPT_READDATA
       are not exported to the java interface */
    return 0;
  }

  return (int) curl_easy_setopt(jcurl->curl, opt, value);
}

static int curljava_write_callback(char *ptr,
                                   size_t size,
                                   size_t nmemb,
                                   void  *handle)
{
  curljava_t *jcurl = (curljava_t *)handle;
  size_t realsize = size * nmemb;
  JNIEnv *env = jcurl->write.env;
  jbyteArray jb=NULL;
  int ret=0;

#ifdef DEBUG
  fprintf(stderr, "%zu bytes data received in callback:\n"
          "ptr=%p, env=%p cls=%p\n",
          realsize, jcurl, env, jcurl->write.cls);
#endif

  jb=(*env)->NewByteArray(env, realsize);
  (*env)->SetByteArrayRegion(env, jb, 0,
                              realsize, (jbyte *)ptr);

#ifdef DEBUG
  fprintf(stderr, "created byte-array\n");
#endif

  ret = (*env)->CallIntMethod(env,
                               jcurl->write.object,
                               jcurl->write.mid,
                               jb);

#ifdef DEBUG
  fprintf(stderr, "java-method returned %d\n", ret);
#endif

  return realsize;
}

/*
 * setopt() int + object
 */

JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1setopt__JILse_haxx_curl_CurlWrite_2
  (JNIEnv *env, jobject myself, jlong handle, jint option, jobject object)
{
  curljava_t *jcurl = (curljava_t *)handle;
  jclass cls_local = (*env)->GetObjectClass(env, object);
  jmethodID mid;
  jclass cls;
  jobject obj_global;

  switch(option) {
  case CURLOPT_WRITEFUNCTION:
    /* this makes a reference that'll be alive until we kill it! */
    cls = (*env)->NewGlobalRef(env, cls_local);

#ifdef DEBUG
    printf("setopt int + object, option = %d cls= %p\n",
           option, cls);
#endif

    if(!cls) {
      fputs("couldn't make local reference global\n", stderr);
      return 0;
    }

    /* this is the write callback */
    mid = (*env)->GetMethodID(env, cls, "handleString", "([B)I");
    if(!mid) {
      fputs("no callback method found\n", stderr);
      return 0;
    }

    obj_global = (*env)->NewGlobalRef(env, object);

    jcurl->write.mid = mid;
    jcurl->write.cls = cls;
    jcurl->write.object = obj_global;
    /*jcurl->write.env = env; stored on perform */

#ifdef DEBUG
    fprintf(stderr,
            "setopt write callback and write file pointer %p, env = %p\n",
            jcurl, env);
#endif

    curl_easy_setopt(jcurl->curl, CURLOPT_WRITEFUNCTION,
                     curljava_write_callback);
    curl_easy_setopt(jcurl->curl, CURLOPT_FILE,
                     jcurl);

    break;
  }
  return 0;
}


static int curljava_read_callback(char *ptr,
                                  size_t size,
                                  size_t nmemb,
                                  void  *handle)
{
  curljava_t *jcurl = (curljava_t *)handle;
  size_t realsize = size * nmemb;
  JNIEnv *env = jcurl->read.env;
  jbyteArray jb;
  int ret;

  jb = (*env)->NewByteArray(env, realsize);
  ret = (*env)->CallIntMethod(env, jcurl->read.object, jcurl->read.mid, jb);

  if (ret > 0) {
    if (ret > realsize)
      ret = realsize;

    (*env)->GetByteArrayRegion(env, jb, 0, ret, ptr);
    }

  return ret;
}

/*
 * setopt() int + object
 */

JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1setopt__JILse_haxx_curl_CurlRead_2
  (JNIEnv *env, jobject myself, jlong handle, jint option, jobject object)
{
  curljava_t *jcurl = (curljava_t *)handle;
  jclass cls_local = (*env)->GetObjectClass(env, object);
  jmethodID mid;
  jclass cls;
  jobject obj_global;

  switch(option) {
  case CURLOPT_READFUNCTION:
    /* this makes a reference that'll be alive until we kill it! */
    cls = (*env)->NewGlobalRef(env, cls_local);

#ifdef DEBUG
    printf("setopt int + object, option = %d cls= %p\n",
           option, cls);
#endif

    if(!cls) {
      fputs("couldn't make local reference global\n", stderr);
      return 0;
    }

    /* this is the read callback */
    mid = (*env)->GetMethodID(env, cls, "retrieveString", "([B)I");
    if(!mid) {
      fputs("no callback method found\n", stderr);
      return 0;
    }

    obj_global = (*env)->NewGlobalRef(env, object);

    jcurl->read.mid = mid;
    jcurl->read.cls = cls;
    jcurl->read.object = obj_global;

#ifdef DEBUG
    fprintf(stderr,
            "setopt read callback and read file pointer %p, env = %p\n",
            jcurl, env);
#endif

    curl_easy_setopt(jcurl->curl, CURLOPT_READFUNCTION,
                     curljava_read_callback);
    curl_easy_setopt(jcurl->curl, CURLOPT_READDATA,
                     jcurl);

    break;
  }
  return 0;
}

/*
 * setopt() int + object
 */

JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1setopt__JILse_haxx_curl_CurlIO_2
  (JNIEnv *env, jobject myself, jlong handle, jint option, jobject object)
{
  switch(option) {

  case CURLOPT_WRITEFUNCTION:
    return Java_se_haxx_curl_CurlGlue_jni_1setopt__JILse_haxx_curl_CurlWrite_2(env, myself,
                                                     handle, option, object);

  case CURLOPT_READFUNCTION:
    return Java_se_haxx_curl_CurlGlue_jni_1setopt__JILse_haxx_curl_CurlRead_2(env, myself,
                                                    handle, option, object);
  }

  return 0;
}

JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_getinfo
  (JNIEnv *env, jobject value)
{
    return 0;
}

JNIEXPORT jint JNICALL Java_se_haxx_curl_CurlGlue_jni_1perform
  (JNIEnv *env, jobject myself, jlong handle)
{
  curljava_t *jcurl = (curljava_t *)handle;
  jcurl->write.env = env;
  jcurl->read.env = env;
  return (int)curl_easy_perform(jcurl->curl);
}
