#pragma once

/**
 * Report board version, identity and health to the Flipper as a block of
 * `BI,<label>,<value>` lines framed by `INFO_BEGIN` / `INFO_END`.
 */
void info_report(void);
