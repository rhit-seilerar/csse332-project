// System call numbers
enum sys_call_id { 
  SYS_fork = 1,
  SYS_exit,
  SYS_wait,
  SYS_pipe,
  SYS_read,
  SYS_kill,
  SYS_exec,
  SYS_fstat,
  SYS_chdir,
  SYS_dup,
  SYS_getpid,
  SYS_sbrk,
  SYS_sleep,
  SYS_uptime,
  SYS_open,
  SYS_write,
  SYS_mknod,
  SYS_unlink,
  SYS_link,
  SYS_mkdir,
  SYS_close,
  SYS_yield,
  SYS_send_signal,
};