linksys_get_rootfs() {
	local rootfsdev
	
    if read cmdline < /proc/cmdline; then
        case "$cmdline" in
        *root=*)
            rootfsdev="${cmdline##*root=}"
            rootfsdev="${rootfsdev%% *}"
            ;;
        esac
        
        echo "${rootfsdev}"
    fi	
}

linksys_update_boot_part() {
    rootfsdev=$1
    
    case "$rootfsdev" in
        "/dev/mmcblk0p15")
            fw_setenv -s - <<-EOF
                boot_part 2
                auto_recovery yes
EOF
            return
            ;;
        "/dev/mmcblk0p17")
            fw_setenv -s - <<-EOF
                    boot_part 1
                    auto_recovery yes
EOF
            return
            ;;
        *)
            return
            ;;
    esac
}

linksys_do_flash() {
	local tar_file=$1
	local kernel=$2
	local rootfs=$3
	
	local overlay_align=$((64*1024))
	
	local board_dir=$(tar tf $tar_file | grep -m 1 '^sysupgrade-.*/$')
	board_dir=${board_dir%/}

	echo "flashing kernel to $kernel"
	tar xf $tar_file ${board_dir}/kernel -O >$kernel

	echo "flashing rootfs to ${rootfs}"
	tar xf $tar_file ${board_dir}/root -O >"${rootfs}"
	
	local squashfs_bytes_used="$(hexdump -e '"0x%02X"'  -n 4 -s 0x28 $rootfs)"
	local offset=$(( (squashfs_bytes_used+(overlay_align-1)) & ~(overlay_align-1) ))
	[ $offset -lt 65536 ] && {
		echo Wrong size for rootfs: $offset
		sleep 10
		reboot -f
	}

	# Mount loop for rootfs_data
	local loopdev="$(losetup -f)"
	losetup -o $offset $loopdev $rootfs || {
		echo "Failed to mount looped rootfs_data."
		sleep 10
		reboot -f
	}

	echo "Format new rootfs_data at position ${offset}."
	mkfs.f2fs -q -l rootfs_data $loopdev
	mkdir /tmp/new_root
	mount -t f2fs $loopdev /tmp/new_root && {
		echo "Saving config to rootfs_data at position ${offset}."
		cp -v /tmp/sysupgrade.tgz /tmp/new_root/
		umount /tmp/new_root
	}

	# Cleanup
	losetup -d $loopdev >/dev/null 2>&1
	sync
	umount -a
	reboot -f
}

platform_do_upgrade_linksys() {
	local tar_file="$1"
	local board=$(board_name)	
	local rootfs kernel
	local magic_long="$(get_magic_long "$tar_file")"
	local target_rootfs="$(linksys_get_rootfs)"	
	
	echo "target root fs $target_rootfs"
	echo "magic long $magic_long"
	
	[ -b "${target_rootfs}" ] || return 1
	
	mkdir -p /var/lock
	case "$target_rootfs" in
        "/dev/mmcblk0p15")
            echo "writing firmware to alternate partition"
            kernel="/dev/mmcblk0p16"
            rootfs="/dev/mmcblk0p17"
            fw_setenv -s - <<-EOF
                boot_part 2
                auto_recovery yes
EOF
            ;;
        "/dev/mmcblk0p17")
            echo "writing firmware to primary partition"
            kernel="/dev/mmcblk0p14"
            rootfs="/dev/mmcblk0p15"
            fw_setenv -s - <<-EOF
                boot_part 1
                auto_recovery yes
EOF
            ;;
        *)
            return 1
            ;;
	esac
	touch /var/lock/fw_printenv.lock

	linksys_do_flash $tar_file $kernel $rootfs

	return 0
}
