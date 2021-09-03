#ifndef __LINUX_STRINGIFY_H
#define __LINUX_STRINGIFY_H

/* 间接字符串化。做两层允许参数本身是一个宏。
 * 例如，使用 -DFOO=bar 编译，__stringify(FOO) 转换为“bar”。
 */

#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)

#endif	/* !__LINUX_STRINGIFY_H */
