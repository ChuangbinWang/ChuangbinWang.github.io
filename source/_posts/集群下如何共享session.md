---
title: 集群下如何共享session
date: 2019-09-20 15:58:11
categories: 
    -后端知识
tags:
    -服务器
    -分布式
    -session同步
---
Session是在服务端保存的一个数据结构，用来跟踪用户的状态，这个数据可以保存在集群、数据库、文件中；
session的用途：
![pic_1](/images/distributed_system/session1.png)
session的实现方法
![pic_2](/images/distributed_system/session2.png)

Cookie是客户端保存用户信息的一种机制，用来记录用户的一些信息，也是实现Session的一种方式，Cookie是浏览器保存信息的一种方式，可以理解为一个文件，保存到客户端了啊，服务器可以通过响应浏览器的set-cookie的标头，得到Cookie的信息。你可以给这个文件设置一个期限，这个期限呢，不会因为浏览器的关闭而消失啊。其实大家应该对这个效果不陌生，很多购物网站都是这个做的，即使你没有买东西，他也记住了你的喜好，现在回来，会优先给你提交你喜欢的东西；
用express 来实现cookies知识的学习
https://blog.csdn.net/henni_719/article/details/53612499

cookies和session的区别

![pic_3](/images/distributed_system/session3.png)

相关知识：
cookie的存活期：默认为-1
会话Cookie：当存活期为负数，把Cookie保存到浏览器上
持久Cookie：当存活期为正数，把Cookie保存到文件中

有人问，如果客户端的浏览器禁用了 Cookie 怎么办？
一般这种情况下，会使用一种叫做URL重写的技术来进行会话跟踪，即每次HTTP交互，URL后面都会被附加上一个诸如 sid=xxxxx 这样的参数，服务端据此来识别用户。
该方法的实现机制为：
● 先判断当前的 Web 组件是否启用 Session，如果没有启用 Session，直接返回参数 url。
● 再判断客户端浏览器是否支持 Cookie，如果支持 Cookie，直接返回参数 url；如果不支持 Cookie，就在参数 url 中加入 Session ID 信息，然后返回修改后的 url。

大部分手机浏览器不支持cookies

集群下共享session 的方法：
1. 客户端cookie加密
当用户登陆成功以后，把网站域名、用户名、密码、token、 session有效时间全部采用cookie的形式写入到客户端的cookie里面。如果用户从一台Web服务器跨越到另一台服务器的时候，我们的程序主动去检测客户端的cookie信息，进行判断，然后提供对应的服务，当然，如果cookie过期，或者无效，自然就不让用户继续服务了。

优点：
简单、高效，也不会加大数据库的负担。
缺点：
cookie安全性较低，虽然它已经加了密，但是还是可以伪造的。
但是如果客户端把cookie禁掉了的话，那么session就无从同步了，这样会给网站带来损失；
cookies中数据不能太多，最好只有个用户id。

或者这个方法： 使用Nginx的ip_hash策略来做负载均衡
ip_hash策略的原理：根据Ip做hash计算，同一个Ip的请求始终会定位到一台web服务器上。

用户---> 浏览器 ----> Nginx（ip_hash） ----->  n（多）台Web服务器

同一个用户（Ip）---> 浏览器 ----> Nginx（ip_hash） -----> A Web服务器
此时假如用户IP为 192.168.1.110 被hash到 A web服务器，那么用户的请求就一直会访问A web服务器了。
优点：

配置简单，只需要在Nginx中配置好ip_hash 策略
对应用无侵入性，应用不需要改任何
ip_hash策略会很均匀的讲用户请求的ip分配到web服务器上，并且可以动态的水平扩展web服务器（只需在配置中加上服务器即可）

缺点:
假如有一台Tomcat 宕机或者重启，那么这一台服务器上的用户的Session信息丢失，导致单点故障。

2. session replication，如tomcat自带session共享，主要是指集群环境下，多台应用服务器之间同步session，使session保持一致，对外透明。 如果其中一台服务器发生故障，根据负载均衡的原理，调度器会遍历寻找可用节点，分发请求，由于session已同步，故能保证用户的session信息不会丢失，会话复制,。
优点：
通过应用服务器配置即可，无需改动代码。
缺点：
性能随着服务器增加急剧下降，而且容易引起广播风暴；
session数据需要序列化，影响性能。
Session同步会有延迟，会影带宽
受限于内存资源，在大用户量，高并发，高流量场景，会占用大量内存，不适合！

session复制的原理
http://book.51cto.com/art/201202/319431.htm

session复制的拓扑结构：
点对点：

![pic_4](/images/distributed_system/session4.png)
点对点复制的拓扑结构的优势：
是不需要额外的进程和产品来避免单点失败，从而减少了配置和维持额外进程或产品的时间和费用的成本。
劣势：
1. 但这个拓扑结构的局限性就是它需要占用一定的内存空间，因为每个服务端都需要备份这个复制域里所有 session 的信息。假如一个 session 需要 10KB 的空间来存储信息，那么当 100 万个用户同时登陆到这个系统中时，每个应用服务器就需要花费 10GB 的内存空间来保存所有 session 的信息。		
2. 它的另一个局限性是每一个 session 信息的修改都会被复制到其他所有的应用服务器上，这极大地影响了整个系统的性能。

3.使用数据库保存session
这种共享session的方式即将session信息存入数据库中，其它应用可以从数据库中查出session信息。
优点：
使用数据库来保存session,就算服务器宕机了也没事，session照样在。
缺点：
每次请求都需要对数据库进行读写，session的并发读写在数据库中完成，会加大数据库的IO，对数据库性能要求比较高。
我们需要额外地实现session淘汰逻辑代码，即定时从数据库表中更新和删除session信息，增加了工作量。
数据库读写速度较慢，不利于session的适时同步。

4. 使用redis或memcache来保存session
提供一个集群保存session共享信息，其他应用统统把自己的session信息存放到session集群服务器组。当应用系统需要session信息的时候直接到session群集服务器上读取。目前大多都是使用Memcache或redis来对Session进行存储。以这种方式来同步session，不会加大数据库的负担，并且安全性比用cookie大大的提高，把session放到内存里面，比从文件中读取要快很多。 
