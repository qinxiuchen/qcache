1.��Ŀ��ҳ��https://github.com/qinxiuchen/qcache
2.��Ŀ��飺qcache��һ��php��չ����php����������ʱ�������ص������ļ���Ϣ���ص����̿ռ��в���פ�ڴ棬�������Լӿ��ѯ���ٶȣ�������������������ļ����龰��
	qcache�İ�װ�����ã�
	1��������ĿĿ¼��ִ�����������
		 >> phpize
		 >> ./configure
		 >> make
		 >> make install
	2������php.ini,�������µ�����
		 extension = qcache.so
		 ;ָ�������ļ��Ĵ��·����php����������ʱ����load��·����������.data��β���ļ��ڵ����ݡ�
		 qcache.data_path = "/tmp/"
3.qcache��ʹ��:
	1��qcache_fetch($key)										//ȡ�ö�Ӧkey�µ���������,��items
	2��qcache_fetch_child($key, $child)			//ȡ�ö�Ӧkey��ĳ���ӽڵ������,��items�µ�items_xxxx
4.����.data�ļ���
	����ĿĿ¼����һ��genData.php���ļ�����������.data�ļ�������php����������ʱ��zend������δ����������data�ļ��洢������Ϊ���л���php���ݸ�ʽ��

5.���������ļ���������ʱ��ֻ��Ҫ����genData.php�ļ���������.data�����ļ�����ʱ��Ҫ��һ���ű�ȥ����reload��/*php���̻��Զ���֪�����ݱ仯���Զ����¼������ݡ�*/