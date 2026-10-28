package main

import "testing"

func TestTrackListParsesBothForms(t *testing.T) {
 for _, test := range []struct{ json string; want string }{
  {`{"result":{"library":{"tracks":[{"id":"42"},{"id":99}]}}}`, "42"},
  {`{"result":{"tracks":[{"id":123}]}}`, "123"},
 } {
  got,err:=tracks([]byte(test.json));if err!=nil||!got[test.want]{t.Fatalf("list: %v %v",got,err)}
 }
 if _,err:=tracks([]byte(`{"result":{}}`));err==nil{t.Fatal("missing list must not imply no rating")}
}
func TestTrackIDIsConstrained(t *testing.T) {
 for _,id:=range []string{"","1/other","-1","1234567890123456789"," 12"}{if validID(id){t.Fatalf("accepted %q",id)}}
 if !validID("153153593"){t.Fatal("valid track rejected")}
}
