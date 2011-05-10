  /*
 * GeoIP C library binding for nodeje
 *
 * Licensed under the GNU LGPL 2.1 license
 */                                          

#include "region.h"

void geoip::Region::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(2);
  constructor_template->SetClassName(String::NewSymbol("geoip"));

  NODE_SET_PROTOTYPE_METHOD(constructor_template, "lookup", lookup);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "lookupSync", lookupSync);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "close", close);
  target->Set(String::NewSymbol("Region"), constructor_template->GetFunction());
}

/*
   Region() :
   db_edition(0)
   {
   }

   ~Region()
   {
   } */

Handle<Value> geoip::Region::New(const Arguments& args)
{
  HandleScope scope;
  Region * r = new Region();

  Local<String> host_str = args[0]->ToString();
  char host_cstr[host_str->Length()];
  host_str->WriteAscii(host_cstr);
  bool cache_on = args[1]->ToBoolean()->Value(); 

  r->db = GeoIP_open(host_cstr, cache_on?GEOIP_MEMORY_CACHE:GEOIP_STANDARD);

  if (r->db != NULL) {
    // Successfully opened the file, return 1 (true)
    r->db_edition = GeoIP_database_edition(r->db);
    if (r->db_edition == GEOIP_REGION_EDITION_REV0 || 
        r->db_edition == GEOIP_REGION_EDITION_REV1) {
      r->Wrap(args.This());
      return scope.Close(args.This());
    } else {
      GeoIP_delete(r->db);	// free()'s the gi reference & closes its fd
      r->db = NULL;
      printf("edition is %d", r->db_edition);
      return scope.Close(ThrowException(String::New("Error: Not valid region database")));
    }
  } else {
    return scope.Close(ThrowException(String::New("Error: Cao not open database")));
  }
}

Handle<Value> geoip::Region::lookupSync(const Arguments &args) {
  HandleScope scope;

  Local<String> host_str = args[0]->ToString();
  Local<Object> data = Object::New();
  char host_cstr[host_str->Length()];
  host_str->WriteAscii(host_cstr);
  Region* r = ObjectWrap::Unwrap<Region>(args.This());

  uint32_t ipnum = _GeoIP_lookupaddress(host_cstr);

  if (ipnum <= 0) {
    return scope.Close(Null());
  }

  GeoIPRegion *region = GeoIP_region_by_ipnum(r->db, ipnum);

  if (region != NULL) {
    data->Set(String::NewSymbol("country_code"), String::New(region->country_code));
    data->Set(String::NewSymbol("region"), String::New(region->region));
    return scope.Close(data);
  } else {
    return scope.Close(ThrowException(String::New("Error: Can not find match data")));
  }
}

Handle<Value> geoip::Region::lookup(const Arguments& args)
{
  HandleScope scope;

  REQ_FUN_ARG(1, cb);

  Region * r = ObjectWrap::Unwrap<Region>(args.This());
  Local<String> host_str = args[0]->ToString();

  region_baton_t *baton = new region_baton_t();

  baton->r = r;
  host_str->WriteAscii(baton->host_cstr);
  baton->increment_by = 2;
  baton->sleep_for = 1;
  baton->cb = Persistent<Function>::New(cb);

  r->Ref();

  eio_custom(EIO_Region, EIO_PRI_DEFAULT, EIO_AfterRegion, baton);
  ev_ref(EV_DEFAULT_UC);

  return scope.Close(Undefined());
}

int geoip::Region::EIO_Region(eio_req *req)
{
  region_baton_t *baton = static_cast<region_baton_t *>(req->data);

  sleep(baton->sleep_for);

  uint32_t ipnum = _GeoIP_lookupaddress(baton->host_cstr);
  if (ipnum <= 0) {
    baton->region = NULL;
  } else {
    baton->region = GeoIP_region_by_ipnum(baton->r->db, ipnum);
  }

  return 0;
}

int geoip::Region::EIO_AfterRegion(eio_req *req)
{
  HandleScope scope;

  region_baton_t * baton = static_cast<region_baton_t *>(req->data);
  ev_unref(EV_DEFAULT_UC);
  baton->r->Unref();

  Local<Value> argv[1];
  if (baton->region != NULL) {
    Local<Object> data = Object::New();
    data->Set(String::NewSymbol("country_code"), String::New(baton->region->country_code));
    data->Set(String::NewSymbol("region"), String::New(baton->region->region));
    argv[0] = data;
  }

  TryCatch try_catch;

  baton->cb->Call(Context::GetCurrent()->Global(), 1, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  baton->cb.Dispose();

  delete baton;
  return 0;
}

Handle<Value> geoip::Region::close(const Arguments &args) {
  Region * r = ObjectWrap::Unwrap<Region>(args.This()); 
  GeoIP_delete(r->db);	// free()'s the gi reference & closes its fd
  r->db = NULL;
  HandleScope scope;	// Stick this down here since it seems to segfault when on top?
}

Persistent<FunctionTemplate> geoip::Region::constructor_template;