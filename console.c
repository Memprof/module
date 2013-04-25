#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>

int printu(const char *fmt, ...) {
   struct tty_struct *my_tty;
   va_list args;
   int ret;
   char buf[512];

   va_start(args, fmt);
   ret=vsprintf(buf,fmt,args);
   va_end(args);

   my_tty = current->signal->tty;
   if (my_tty != NULL) {
      int len = strlen(buf);
      if(len > 1 && buf[len - 1] == '\n' && buf[len - 2] != '\r') {
         buf[len - 1] = '\r';
         buf[len] = '\n';
         buf[len + 1] = '\0';
         len += 1;
      }
      ((my_tty->driver->ops)->write) (my_tty,  buf, len);
   }
   return 0;
}
