---
title: elasticsearch底层存储原理
date: 2019-09-23 10:59:38
categories: 
    -elasticsearch
    -数据库
tags:
    -底层原理
    -elasticsearch
---

Elasticsearch 是一个建立在全文搜索引擎 Apache Lucene(TM) 基础上的搜索引擎，他能将每一个字段都编入索引，使其可以被搜索。本文的核心是探讨其作为搜索引擎的内部核心存储机制。

本文总结自
https://neway6655.github.io/elasticsearch/2015/09/11/elasticsearch-study-notes.html
https://www.infoq.cn/article/database-timestamp-02/?utm_source=infoq&utm_medium=related_content_link&utm_campaign=relatedContent_articles_clk

Elasticsearch是**面向文档型数据库**，一条数据在这里就是一个文档，该文档属于一个User的类型，各种各样的类型会属于一个索引。其中用**JSON作为文档序列化的格式**，比如下面这条用户数据：
{
    "name" : "John",
    "sex" : "Male",
    "age" : 25,
    "birthDate": "1990/05/01",
    "about" : "I love to go rock climbing",
    "interests": [ "sports", "music" ]
}
Elasticsearch和关系型数据术语对照表:
关系数据库 ⇒ 数据库 ⇒ 表 ⇒ 行 ⇒ 列(Columns)
Elasticsearch ⇒ 索引 ⇒ 类型 ⇒ 文档 ⇒ 字段(Fields)

Elasticsearch最关键的就是提供强大且丰富的索引功能，它的索引思路，**就是将磁盘里的东西尽量搬进内存，减少磁盘随机读取次数(同时也利用磁盘顺序读特性)**，结合各种奇妙的压缩算法，用及其苛刻的态度使用内存。当然了在提高搜索的性能的同时，难免会牺牲某些其他方面的性能，比如插入/更新，因为在插入数据的同时它还会为每个字段建立倒排索引。

在传统的数据库中，由于二叉树的查找效率是logN，插入和删除的效率高，且在树的结构下插入新的节点不会移动全部节点，又结合于磁盘读取特性，传统的数据库使用B-Tree/B+-Tree。
![](/images/sql/clipboard.png)

为了提高查询的效率，减少磁盘寻道次数，将多个值作为一个数组通过连续区间存放，一次寻道读取多个数据，同时也降低树的高度。

建立倒排索引的流程：
![](/images/sql/clipboard1.png)

示例流程：
![](/images/sql/clipboard2.png)

得到的倒排索引如下:
![](/images/sql/clipboard3.png)

Posting List
Elasticsearch分别为每个field都建立了一个倒排索引，**Kate, John, 24, Female这些叫term（term的定义）**，而[1,2]就是Posting List。Posting list就是一个int的数组，存储了所有符合某个term的文档id。
通过posting list这种索引方式似乎可以很快进行查找，比如要找age=24的同学，爱回答问题的小明马上就举手回答：我知道，id是1，2的同学。但是，如果这里有上千万的记录呢？如果是想通过name来查找呢？

在传统的数据库中使用B-Tree减少磁盘寻道次数，直接将索引用一个b-tree树连接在一起并存入一个数据页中，而elasticsearch也使用同样的方法，它首先将term进行排序，并用字典树的方法将其连接起来形成term index。
而建立这些term和文档id 的关系就需要前面两步，term index，和建立term dictionary

Term index这棵树不会包含所有的term，它只包含的是term的一些前缀。主要是由于如果term太多，term index也会很大，放内存不现实。
在mysql中是把索引存入硬盘，然后需要若干次读取磁盘进行数据寻找。
![](/images/sql/clipboard4.png)

**而该字典树又像B-tree一样会存储该节点内的具体信息，类似，它的每个节点会存储该term index下的term dictionary blocks信息，而每个term dictionary block又会执行其对应的Posting List信息。**
![](/images/sql/clipboard5.png)

**再结合FST(Finite State Transducers)的压缩技术，可以使term index缓存到内存中。从term index查到对应的term dictionary的block位置之后，再去磁盘上找term，大大减少了磁盘随机读的次数。**

那FST压缩算法是什么。它用在什么地方呢？
FSTs are finite-state machines that map a term (byte sequence) to an arbitrary output.
FSTs是有限状态机，它将术语(字节序列)映射到任意输出。
画个图你就知道。

假设我们现在要将mop, moth, pop, star, stop and top(term index里的term前缀)映射到序号：0，1，2，3，4，5(term dictionary的block位置)。最简单的做法就是定义个Map<String, Integer>，然后将对应数据存入map中，但是这样太消耗内存。而FST就是将单词分成单个字母通过⭕️和–>表示出来，0权重不显示。如果⭕️后面出现分支，就标记权重，最后整条路径上的权重加起来就是这个单词对应的序号。
![](/images/sql/clipboard6.png)

⭕️ 表示一种状态
–>表示状态的变化过程，上面的字母/数字表示状态变化和权重

**FST就用于term index字典树和term index指向term dictionary blocks时 term dictionary blocks的存储。**

ES还对posting list 做了压缩，其压缩方法是：Frame Of Reference， Roaring bitmaps

Frame Of Reference：**原理就是通过增量，将原来的大数变成小数仅存储增量值，再精打细算按bit排好队，最后通过字节存储，而不是大大咧咧的尽管是2也是用int(4个字节)来存储。**
![](/images/sql/clipboard7.png)

这个 Frame of Reference 的编码是有解压缩成本的。当进行查找的时候可以利用 skip list，除了跳过了遍历的成本，也跳过了解压缩这些压缩过的 block 的过程，从而节省了 cpu。

Roaring bitmaps
传统的Bitmap是一种数据结构，假设有某个posting list：[1,3,4,7,10]
对应的bitmap就是：[1,0,1,1,0,0,1,0,0,1]
虽然这个方法已经相对好，但还不算很压缩率高，且其缺点是存储空间随着 变量个数线性增长。
而Roaring bitmaps，将posting list按照65535为界限分块，比如第一块所包含的文档id范围在0~65535之间，第二块的id范围是65536~131071，以此类推。再用<商，余数>的组合表示每一组id，这样每组里的id范围都在0~65535内了，剩下的就好办了，既然每组id不会变得无限大，那么我们就可以通过最有效的方式对这里的id存储。
![](/images/sql/clipboard8.png)

可是为什么是以65535为界限?

65535也是一个经典值，因为它=2^16-1，**正好是用2个字节能表示的最大数（选用2个字节的原因是大部分情况，设置一个字节那最多表示256-1 个数，区间的长度太小，而 三个字节又比较难用short或者char表示，选用4个组件那区间的空间太大不合适）**，一个short的存储单位，注意到上图里的最后一行“If a block has more than 4096 values（如果这个块里面有超过4096个值时）, encode as a bit set, and otherwise as a simple array using 2 bytes per value”，如果是大块，用节省点用bitset存，小块就豪爽点，2个字节我也不计较了，用一个short[]存着方便。

那为什么用4096来区分采用数组还是bitmap的阀值呢？

这个是从内存大小考虑的，当block块里元素超过4096后，用bitmap更剩空间：
采用bitmap需要的空间是恒定的: 65536/8 = 8192bytes
而如果采用short[]，所需的空间是: 2*N(N为数组元素个数)
![](/images/sql/clipboard9.png)

更通俗的讲法是
**由于选用2个字节表示一个最大的数那么其占有的空间 65536/8 = 8192个字节，而如果是小块，且数值最大也是65535，用short类型来表示这个最大数值也只需要2个字节，那么8192个字节，就只能表示4096个数。**

如何减少文档数？
Elasticsearch 有一个功能可以实现类似的优化效果，那就是 Nested Document。我们可以把一段时间的很多个数据点打包存储到一个父文档里，变成其嵌套的子文档。示例如下：

{timestamp:12:05:01, idc:sz, value1:10,value2:11}
{timestamp:12:05:02, idc:sz, value1:9,value2:9}
{timestamp:12:05:02, idc:sz, value1:18,value:17}
可以打包成：
{max_timestamp:12:05:02, min_timestamp: 1205:01, idc:sz,
records: [
{timestamp:12:05:01, value1:10,value2:11}
{timestamp:12:05:02, value1:9,value2:9}
{timestamp:12:05:02, value1:18,value:17}
]}
这样可以把数据点公共的维度字段上移到父文档里，而不用在每个子文档里重复存储，从而减少索引的尺寸。
![](/images/sql/clipboard10.png)


这样** 使用了嵌套文档之后，对于 term 的 posting list 只需要保存父文档的 doc id 就可以了，可以比保存所有的数据点的 doc id 要少很多。如果我们可以在一个父文档里塞入 50 个嵌套文档，那么 posting list 可以变成之前的 1/50。**

联合索引：
上面说了半天都是单field索引，如果多个field索引的联合查询，倒排索引如何满足快速查询的要求呢？
在posting list中
利用跳表(Skip list)的数据结构快速做“与”运算，或者
利用上面提到的bitset按位“与”
先看看跳表的数据结构：
![](/images/sql/clipboard11.png)

将一个有序链表level0，挑出其中几个元素到level1及level2，每个level越往上，选出来的指针元素越少，查找时依次从高level往低查找，比如55，先找到level2的31，再找到level1的47，最后找到55，一共3次查找，查找效率和2叉树的效率相当，但也是用了一定的空间冗余来换取的。

但是在posting list中如果进行联合索引时，由于已经建立好不同长度链表，将其转换为跳表结构体后，最短的posting list中的每个id，逐个在另外两个posting list中查找看是否存在，最后得到交集的结果。
![](/images/sql/clipboard12.png)

当posting list 中的blocking 长度大于4096时，其存储形式是 bitset，那么，直接按位与，得到的结果就是最后的交集。

最后，总结：对于使用Elasticsearch进行索引时需要注意:
1. 不需要索引的字段，一定要明确定义出来，因为默认是自动建索引的
2. 同样的道理，对于String类型的字段，不需要analysis的也需要明确定义出来，因为默认也是会analysis的
3. 选择有规律的ID很重要，随机性太大的ID(比如java的UUID)不利于查询