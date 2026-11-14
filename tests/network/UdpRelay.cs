using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading;

// A loopback-only fault injector. It never accepts or forwards remote traffic.
public static class StarfallUdpRelay {
    sealed class Packet { public byte[] Data; public int Side; public long Due; }
    public static void Run(int port, string directory) {
        var sockets = new[] {
            new UdpClient(new IPEndPoint(IPAddress.Loopback, port + 2)),
            new UdpClient(new IPEndPoint(IPAddress.Loopback, port + 3))
        };
        // The first peer starts before the second binds. Ignore Windows' UDP
        // connection-reset notification for those initial ICMP port-unreachable
        // replies; later datagrams must continue to use the same sockets.
        foreach(var socket in sockets)
            socket.Client.IOControl((IOControlCode)unchecked((int)0x9800000C), new byte[4], null);
        var clock = Stopwatch.StartNew();
        var pending = new List<Packet>();
        long received=0, forwarded=0, dropped=0, blackout=0, first=-1, lastReport=-1;
        var directions = new long[2];
        Action report = () => File.WriteAllText(Path.Combine(directory,"relay.json"),
            "{\"received\":"+received+",\"forwarded\":"+forwarded+",\"dropped\":"+dropped+
            ",\"blackoutDropped\":"+blackout+",\"peer0Forwarded\":"+directions[0]+
            ",\"peer1Forwarded\":"+directions[1]+",\"delayMs\":50,\"jitterMs\":21,\"dropEvery\":23,\"blackoutMs\":2000}");
        File.WriteAllText(Path.Combine(directory,"relay.ready"),"127.0.0.1");
        try {
            while(clock.ElapsedMilliseconds < 240000 && !File.Exists(Path.Combine(directory,"relay.stop"))) {
                long now=clock.ElapsedMilliseconds;
                for(int side=0;side<2;side++) {
                    while(sockets[side].Available>0) {
                        IPEndPoint source = new IPEndPoint(IPAddress.Any,0);
                        byte[] bytes=sockets[side].Receive(ref source);
                        if(!source.Address.Equals(IPAddress.Loopback) || source.Port!=port+side) continue;
                        if(first<0) first=now;
                        received++;
                        if(now-first>=8000 && now-first<10000) {blackout++;continue;}
                        if(received%23==0) {dropped++;continue;}
                        pending.Add(new Packet {Data=bytes,Side=side,Due=now+50+(received%4)*7});
                    }
                }
                for(int i=pending.Count-1;i>=0;i--) if(pending[i].Due<=now) {
                    var packet=pending[i];
                    int other=1-packet.Side;
                    sockets[other].Send(packet.Data,packet.Data.Length,new IPEndPoint(IPAddress.Loopback,port+other));
                    forwarded++;directions[packet.Side]++;pending.RemoveAt(i);
                }
                if(now-lastReport>500) {report();lastReport=now;}
                Thread.Sleep(1);
            }
        } finally {report();foreach(var socket in sockets) socket.Dispose();}
    }
}
