package pipeline

// Phase 5 of the build pipeline. Packs the Android boot.img (when
// the device profile requests one) and creates the final partitioned
// disk image from the populated rootfs.

import (
	"fmt"
	"path/filepath"

	"peacock/internal/builder"
	"peacock/internal/config"
	"peacock/internal/image"
	"peacock/internal/manifest"
)

// runImageAssemblyPhase performs phase 5 and writes the final per-device
// .img into workDir. Returns the final image path. The error return is
// fatal; caller prints + cleans up.
func (r *Runner) runImageAssemblyPhase(
	b *builder.Builder,
	dev *manifest.Device,
	imageChrootRoot string,
	rootfsPath string,
	kernelBuildDir string,
	initramfsPath string,
	emptyRootfs bool,
	workDir string,
) (string, error) {
	deviceName := r.opts.Device
	imagePath := filepath.Join(workDir, fmt.Sprintf("%s.img", deviceName))

	if kernelBuildDir != "" && dev.Boot.GenerateBootImg {
		fmt.Println("Generating Android boot.img...")
		bootImgPath := filepath.Join(workDir, "boot.img")

		zImagePath := filepath.Join(kernelBuildDir, "zImage")
		cmdline := dev.Boot.Cmdline

		parseHex := func(s string) (uint32, error) {
			var val uint32
			_, err := fmt.Sscanf(s, "0x%x", &val)
			if err != nil {
				_, err = fmt.Sscanf(s, "%x", &val)
			}
			return val, err
		}

		baseAddr, err := parseHex(dev.Boot.Android.Base)
		if err != nil {
			fmt.Printf("Error parsing base address %s: %v, using default 0x80200000\n", dev.Boot.Android.Base, err)
			baseAddr = 0x80200000
		}

		kernelOffset, err := parseHex(dev.Boot.Android.KernelOffset)
		if err != nil {
			kernelOffset = 0x00008000
		}

		ramdiskOffset, err := parseHex(dev.Boot.Android.RamdiskOffset)
		if err != nil {
			ramdiskOffset = 0x02000000
		}

		secondOffset, err := parseHex(dev.Boot.Android.SecondOffset)
		if err != nil {
			secondOffset = 0x00f00000
		}

		tagsOffset, err := parseHex(dev.Boot.Android.TagsOffset)
		if err != nil {
			tagsOffset = 0x00000100
		}

		pageSize := uint32(dev.Boot.Android.PageSize)
		if pageSize == 0 {
			pageSize = 2048
		}

		if err := image.CreateBootImage(bootImgPath, zImagePath, initramfsPath, cmdline, baseAddr, kernelOffset, ramdiskOffset, secondOffset, tagsOffset, pageSize); err != nil {
			fmt.Printf("Error creating boot.img: %v\n", err)
		} else {
			fmt.Printf("Boot image created at: %s\n", bootImgPath)
		}
	}

	fmt.Println("Creating disk image...")
	imageSizeMB := config.ImageSizeMB()
	if imageSizeMB <= 0 {
		imageSizeMB = estimateImageSizeMB(rootfsPath, emptyRootfs)
		fmt.Printf("Auto image size: %dMB\n", imageSizeMB)
	}
	if err := b.CreateDiskImage(imageChrootRoot, rootfsPath, imagePath, imageSizeMB, dev.Quirks.LegacyRootfsExt4); err != nil {
		return "", fmt.Errorf("error creating disk image: %w", err)
	}

	return imagePath, nil
}
