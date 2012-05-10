1.项目主页：https://github.com/qinxiuchen/qcache
2.项目简介：qcache是一个php扩展，在php主进程启动时，会把相关的配置文件信息加载到进程空间中并常驻内存，这样可以加快查询的速度，适用于有许多大的配置文件的情景。
	qcache的安装与配置：
	1）进入项目目录，执行下面的命令
		 >> phpize
		 >> ./configure
		 >> make
		 >> make install
	2）配置php.ini,增加如下的配置
		 extension = qcache.so
		 ;指定数据文件的存放路径，php主进程启动时，会load该路径下所有以.data结尾的文件内的数据。
		 qcache.data_path = "/tmp/"
3.qcache的使用:
	1）qcache_fetch($key)										//取得对应key下的所有配置,如items
	2）qcache_fetch_child($key, $child)			//取得对应key下某个子节点的配置,如items下的items_xxxx
4.生成.data文件：
	在项目目录下有一个genData.php的文件，用于生成.data文件。由于php主进程启动时，zend引擎尚未启动，所以data文件存储的数据为序列化的php数据格式。

5.当有数据文件发生更新时，只需要调用genData.php文件重新生成.data数据文件，暂时需要跑一个脚本去重新reload。/*php进程会自动感知到数据变化，自动重新加载数据。*/