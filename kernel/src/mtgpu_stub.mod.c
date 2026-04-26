#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

KSYMTAB_DATA(mtgpu_drm_driver_fops, "", "");

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v00001ED5d00000102sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001ED5d00000103sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "0206CA2D07429A0D0CCF811");
