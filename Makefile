ifneq ($(KERNELRELEASE),)
	ifeq ($(PATCHLEVEL),6)
		EXTRA_CFLAGS	+= -DKERNEL26
	endif
	obj-m	:= lmpcm_usb.o
endif

KVER	:= $(shell uname -r)
KDIR	:= /lib/modules/$(KVER)
KSDIR	:= $(KDIR)/build
PWD	:= $(shell pwd)


default:
	$(MAKE) -C $(KSDIR) SUBDIRS=$(PWD) modules

modules: $(obj-m)

install:
	@if [ ! -d "$(KDIR)/misc" ]; then \
		mkdir "$(KDIR)/misc"; \
	fi; \
	case "$(KVER)" in \
		*.4.*) \
			cp lmpcm_usb.o "$(KDIR)/misc/"; \
			;; \
		*.6.*) \
			cp lmpcm_usb.ko "$(KDIR)/misc/"; \
			;; \
		*) \
			echo "## Unsupported kernel version"; \
			exit -1; \
			;; \
	esac; \
#	depmod -A

clean:
	$(RM) -r *.mod.c *.o *.ko *.symvers .lmpcm_usb.* .tmp_versions modules.order
