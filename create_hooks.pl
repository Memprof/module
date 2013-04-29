#!/usr/bin/perl
use warnings;
use strict;
use File::stat;

die('Usage: ./create_hooks.pl directory') if(!$ARGV[0]);


#The symbols we want to find and the file where to put them
my $hook_file_name = $ARGV[0].'/hooks.h';
my %fun_to_look_for = (
   "setup_APIC_eilvt" => "int (*)(u8, u8, u8, u8)",
   "perf_event_mmap" => "void (*)(struct vm_area_struct *)",
   "perf_event_task" => "void (*)(struct task_struct *, void*, int)",
   "perf_event_comm" => "void (*)(struct task_struct *)",
   "text_poke" => "void* (*)(void *, const void *, size_t)",
   "perf_event_task_ctx" => "void (*)(void*, void*)",
   "perf_event_fork" => "void (*)(struct task_struct *)",
   "perf_event_exit_task" => "void (*)(struct task_struct *)",
);
my %var_to_look_for = (
   "text_mutex" => "struct mutex",
   "tasklist_lock" => "rwlock_t",
);


#Try to find symbols in one of those files
#Exit if we have the most recent version of the symbols
my $kernel = `uname -r`; chomp($kernel);
my @symbol_files = ("/boot/System.map-$kernel" , "/boot/System.map-genkernel-x86_64-$kernel");

my $modif_time_hooks = 0;
if(-e $hook_file_name) {
   $modif_time_hooks = stat($hook_file_name)->mtime;
}
#Ignore hook file if the .pl has been updated
my $my_modif_time = stat($0)->mtime;
if($my_modif_time > $modif_time_hooks) {
   $modif_time_hooks = undef;
}

my $symbols;
for my $s (@symbol_files) {
   if(-e $s) {
      #if($modif_time_hooks && (stat($s)->mtime < $modif_time_hooks)) {
      #   exit; #Already have latest symbols
      #}
      $symbols = `cat $s`;
   }
}
die("Unable to find kernel symbols. Looked in".join(',', @symbol_files)."\n") if(!$symbols);
my @lines = split(/\n/, $symbols);


#Write symbols in the file
open(F, "> $hook_file_name") or die("Cannot open $hook_file_name for writting\n");
print F "#ifndef MEMPROF_HOOKS\n";
print F "#define MEMPROF_HOOKS\n\n";

for my $l (@lines) {
   next if($l !~ m/([a-f0-9]+)\s(\w)\s(.*)/);
   my $h = $1;
   my $f = $3;
   if($fun_to_look_for{$f} && $2 eq 'T') {
      my $type = $fun_to_look_for{$f};
      my $fun_hook = $type;
      $fun_hook =~ s/(\(\*\))/(*${f}_hook)/;
      print F "static __attribute__((unused)) $fun_hook = ($type) 0x$h;\n";
      #$fun_to_look_for{$f} = undef;
   }
   if($var_to_look_for{$f} && $2 eq 'D') {
      print F "static __attribute__((unused)) $var_to_look_for{$f}* ${f}_hook = (void*)0x$h;\n";
   }
}

print F "\n#endif\n";

