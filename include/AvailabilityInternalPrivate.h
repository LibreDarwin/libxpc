/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 */

/*
 * AvailabilityInternalPrivate.h -- the SPI availability macros.
 *
 * The internal SDK's copy defines __SPI_AVAILABLE and friends on top of
 * availability helpers newer than the public SDK these sources build
 * against, and private headers such as spawn_private.h fail to parse with
 * it.  SPI availability is the same annotation as API availability, so it
 * is spelled with the public SDK's macros instead.
 */

#ifndef __AVAILABILITY_INTERNAL_PRIVATE__
#define __AVAILABILITY_INTERNAL_PRIVATE__

#define __SPI_AVAILABLE(...)		__API_AVAILABLE(__VA_ARGS__)
#define SPI_AVAILABLE(...)		__API_AVAILABLE(__VA_ARGS__)
#define __SPI_AVAILABLE_BEGIN(...)	__API_AVAILABLE_BEGIN(__VA_ARGS__)
#define __SPI_AVAILABLE_END		__API_AVAILABLE_END
#define __SPI_DEPRECATED(...)		__API_DEPRECATED(__VA_ARGS__)
#define SPI_DEPRECATED(...)		__API_DEPRECATED(__VA_ARGS__)
#define __SPI_DEPRECATED_WITH_REPLACEMENT(...) \
	__API_DEPRECATED_WITH_REPLACEMENT(__VA_ARGS__)
#define SPI_DEPRECATED_WITH_REPLACEMENT(...) \
	__API_DEPRECATED_WITH_REPLACEMENT(__VA_ARGS__)
#define __SPI_UNAVAILABLE(...)		__API_UNAVAILABLE(__VA_ARGS__)

#endif /* __AVAILABILITY_INTERNAL_PRIVATE__ */
