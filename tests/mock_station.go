// Local-only Station control stub for visual development. It accepts a dummy
// key and simulates the asynchronous volume confirmation without a real device.
package main

import (
    "bufio"
    "encoding/json"
    "fmt"
    "net"
    "os"
    "strconv"
    "strings"
    "sync"
    "time"
)

func main() {
    listener, err := net.Listen("tcp", "127.0.0.1:27363")
    if err != nil { panic(err) }
    defer listener.Close()
    var mu sync.Mutex
    volume := 1
    empty := os.Getenv("STATION_HUB_MOCK_EMPTY") == "1"
    mockArt := os.Getenv("STATION_HUB_MOCK_ART")
    position := 45000
    if value, err := strconv.Atoi(os.Getenv("STATION_HUB_MOCK_POSITION_MS")); err == nil && value >= 0 && value <= 180000 { position = value }
    fmt.Println("Mock Station listening on 127.0.0.1:27363 (dummy key 0123456789abcdef)")
    for {
        client, err := listener.Accept(); if err != nil { return }
        go func() {
            defer client.Close()
            _ = client.SetDeadline(time.Now().Add(4*time.Second))
            line, _ := bufio.NewReader(client).ReadString('\n')
            parts := strings.SplitN(strings.TrimSpace(line), " ", 2)
            if len(parts)!=2 || parts[0]!="0123456789abcdef" { return }
            command:=parts[1]
            if command!="STATE" { fmt.Printf("Mock command: %s\n",command) }
            reply:=map[string]interface{}{"result":"SENT"}
            if command=="STATE" {
                mu.Lock();v:=volume;mu.Unlock()
                reply=map[string]interface{}{"result":"OK","known":true,"has_media":!empty,"playing":!empty,"title":"Песня для примера","artist":"Station Hub","artwork":"","position_ms":position,"duration_ms":180000,"track_token":"sample","rating":"NONE","rating_status":"OK","volume":v}
                if !empty && mockArt!="" { reply["artwork"]=mockArt }
                if empty { reply["title"]="";reply["artist"]="";reply["track_token"]="";reply["position_ms"]=-1;reply["duration_ms"]=-1 }
            } else if strings.HasPrefix(command,"VOLUME_SET ") {
                v,e:=strconv.Atoi(strings.TrimPrefix(command,"VOLUME_SET "))
                if e!=nil || v<1 || v>10 { reply["result"]="UNAVAILABLE" } else {
                    fmt.Printf("Mock volume request: %d\n",v)
                    go func(){time.Sleep(2*time.Second);mu.Lock();volume=v;mu.Unlock()}()
                }
            }
            _ = json.NewEncoder(client).Encode(reply)
        }()
    }
}
