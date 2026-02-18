/*
 * Copyright (C) 2015 - Florent Revest <revestflo@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <linux/fb.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>

// TODO: We should be able to specify which framebuffer must be used and at which frequency the loop should be executed

int main(int argc, char *argv[])
{
    int ret = 0;
    struct fb_var_screeninfo var;
    int fd = -1;
    int tries = 0;
  
    setpriority(PRIO_PROCESS, 0, -20);

    while (tries++ < 60) {
        fd = open("/dev/fb0", O_RDWR);
        if (fd < 0)
            fd = open("/dev/graphics/fb0", O_RDWR);
        if (fd >= 0)
            break;
        usleep(50000);
    }
    if (fd < 0) {
        perror("Failed to open framebuffer");
        return 1;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror("Failed FBIOGET_VSCREENINFO");
        close(fd);
        return 1;
    }

    if(argc > 1 && !strcmp(argv[1], "--loop"))
    {
        while(1) {
            if (ioctl(fd, FBIOPAN_DISPLAY, &var) < 0 && errno != EBUSY) {
                perror("Failed FBIOPAN_DISPLAY");
                ret = 1;
                break;
            }
            usleep(16666);
        }
    }
    else if(ioctl(fd, FBIOPAN_DISPLAY, &var) < 0)
    {
        perror("Failed FBIOPAN_DISPLAY");
        ret = 1;
    }

    close(fd);

    return ret;
}
