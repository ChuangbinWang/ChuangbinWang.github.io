---
title: webrtc实现流程及原理探索
date: 2019-09-20 16:09:58
categories: 
    -webrtc
    -音视频
    -实战
tags:
    -webrtc
---
目前实时流媒体主流有三种实现方式：WebRTC、HLS、RTMP

HLS:
是有苹果开发的 一种把流媒体拆分成多个独立小文件的技术，按照播放时间请求不同文件，把hls的文件进行解复用之后取出音视频数据然后丢给video去播放（Safari和安卓版的Chrome能直接播放hls）。

它的优点是：使用了传统http协议，所以兼容性和稳定都非常好，服务端可以把hls文件上传到cdn，进而能够应对百万级别观众的直播，
缺点是延时比较大，通常在10s以上，适合观众和主播没有什么交互的场景。因为一个hls文件时间长度通常在10s以上，再加上生成文件的时间就导致延迟很大。

RTMP：
是Adobe推出的，使用长连接，是一套完整的流媒体传输协议，使用flv视频容器，原生浏览器不支持（flash插件支持），不过可以使用websocket + MSE的方式，相关的类库比较少，在Android/IOS客户端上的直播应该用得比较多一点。
相对于HLS请求分片的形式，RTMP由于使用长连接，接收不间断的数据流，它的延迟要比HLS小很多，通常是1~3秒，所以如果观众和主播之间有通话或者视频交互，这种方式的延迟是可以接受的。

WebRTC：
所有主流浏览器都支持，做到比RTMP提供更低的延迟和更小的缓冲率，官方还提供了配套的native的Andorid/IOS的库， 不过实际的实现可能是套一个webview，由webview启动webrtc，再把数据给native层渲染。
![](/images/audio_video/webrtc1.png)
（1）getUserMedia是负责获取用户本地的多媒体数据，如调起摄像头录像等。
（2）RTCPeerConnection是负责建立P2P连接以及传输多媒体数据。
（3）RTCDataChannel是提供的一个信令通道，在游戏里面信令是实现互动的重要元素。

穿墙打洞
要建立一个连接需要知道对方的IP地址和端口号，在局域网里面一台路由器可能会连接着很多台设备，例如家庭路由器接入宽带的时候宽带服务商会分配一个公网的IP地址，所有连到这个路由器的设备都共用这个公网IP地址。如果两台设备都用了同一个端口号创建套接字去连接服务，这个时候就会冲突，因为对外的IP是一样的。因此路由器需要重写IP地址/端口号进行区分，如下图所示：
![](/images/audio_video/webrtc2.webp)
有两台设备分别用了相同的端口号建立连接，被路由器转换成不同的端口，对外网表现为相同IP地址不同端口号，当服务器给这两个端口号发送数据的时候，路由器再根据地址转换映射表把数据转发给相应的主机。
所以当你在本地监听端口号为55020，但是对外的端口号并不是这个，对方用55020这个端口号是连不到你的。这个时候有两种解决方法，第一种是在路由器设置一下端口映射，如下图所示：
![](/images/audio_video/webrtc3.webp)
上图的配置是把所有发往8123端口的数据包到转到192.168.123.20这台设备上。
但是我们不能要求每个用户都这么配他们的路由器，因此就有了穿墙打洞，基本方法是先由服务器与其中一方（Peer）建立连接，这个时候路由器就会建立一个端口号内网和外网的映射关系并保存起来，如上面的外网1091就可以打到电脑的55020的应用上，这样就打了一个洞，这个时候服务器把1091端口加上IP地址告诉另一方（Peer），让它用这个打好洞的地址进行连接。这就是建立P2P连接穿墙打洞的原理，最早起源于网络游戏，因为打网络游戏经常要组网，WebRTC对NAT打洞进行了标准化。

这个的有效性受制于用户的网络拓扑结构，
因为如果路由器的映射关系既取决于内网的IP + 端口号，也取决于服务器的IP加端口号，这个时候就打不了洞了，因为服务器打的那个洞不能给另外一个外网的应用程序使用（会建立不同的映射关系）。
相反如果地址映射表只取决于内网机器的IP和端口号那么是可行的。
打不了洞的情况下WebRTC也提供了解决方法，即用一个服务器转发多媒体数据。

这套打洞的机制叫ICE（Interactive Connectivity Establishment），帮忙打洞的服务器叫TURN服务，转发多媒体数据的服务器叫STUN服务。
谷歌提供了一个turn server（https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/）

除了默认提供的TURN服务打洞之外，还需要有一个websocket服务交换互连双方的信息。
![](/images/audio_video/webrtc4.png)
首先打开摄像头获取到本地的mediaStream，并把它添加到RTCPeerConnection的对象里面，然后创建一个本地的offer，这个offer主要是描述本机的一些网络和媒体信息，采用SDP（ Session Description Protocol）格式。然后把这个offer通过websocket服务发送给要连接的对方，对方收到后创建一个answer，格式、作用和offer一样，发送给呼叫方告知被呼叫方的一些信息。当任意一方收到对方的sdp信息后就会调setRemoteDescription记录起来。而当收到默认的ice server发来的打洞信息candidate之后，把candidate发送给对方（在setRemoteDesc之后），让对方发起连接，成功的话就会触发onaddstream事件，把事件里的event.stream画到video上面即可得到对方的影像。

以上就是建立用webrtc建立p2p通信的过程。

具体的代码执行流程如下
![](/images/audio_video/webrtc5.png)

其中需要注意的实现由：
webrtc 开发之前必须了解的东西
1、创建offer的时候带上参数：{ offerToReceiveAudio: true, offerToReceiveVideo: true }
2、onicecandidate 必须写在 setLocalDescription 之前，因为一调用setLocalDescription，立马会产生icecandidate。
3、pc.addTrack（或者addStream）必须在pc.createAnswer之前，如果你的offer没有带上参数（第一条），那么也应该在pc.createOffer()之前。因为offer或者answer带有媒体信息。
4、webrtc 是 peer to peer ，不是peers to peers。A与B 相连，A需要new RTCPeerConnection，B也需要。A再与C相连，A还需要new  RTCPeerConnection
5、stun 服务器，是提供打洞的东西，turn服务器是提供数据中转的东西。打洞（Nat类型及穿透）请看我的另一篇博客，webrtc只支持 https和 localhost，所以局内网主机能开视频，另一端（另一台电脑）只能看。
6、火狐报错：xxx failed（需要turn但根本没有turn服务器），还有一种，turn Server appears to be broken：这个是turn服务器限制连接数量，就是说人较多，你挤不上去了。
免费的turn 是http://numb.viagenie.ca，去申请账号密码。
免费的stun : stun:stun.freeswitch.org 、stun.ekiga.net
自己部turn: github 搜 coturn
个人网页：gusheng123.top，可测试多对多视频。


其中废弃的api有
navigator.getUserMeida(已废弃)，现在改为navigator.mediaDevices.getUserMedia;
RTCPeerConnection.addStream被RTCPeerConnection.addTrack取代;
STUN,TURN配置里的url现被urls取代；

webrtc的api文档
https://www.kaifaxueyuan.com/frontend/webrtc/webrtc-rtcpeerconnection-apis.html

sdp消息分享后，在进行连接之前，会进行候选者检索。
![](/images/audio_video/webrtc6.png)
本机候选者

进行webrtc的端口转换的协议叫做STUN协议，用于收集，srflx (可用于p2p)的候选者
TURN服务器，又叫中继服务器，用于收集中继类型的 webrtc候选者。
![](/images/audio_video/webrtc7.png)
李超教程p2p直播间流程图
![](/images/audio_video/webrtc8.jpg)

当调用createOffer和createAnswer时就会自己触发 onicecandidate 事件