/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Tobias Zirr

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

float
blinn_phong_based_brdf(vec3 V, vec3 L, vec3 N, float phong_exp)
{
	vec3 H = normalize(V + L);
		float F = pow(1.0 - max(0.0, dot(H, V)), 5.0);
		return mix(0.15, 0.05 + 10.25 * pow(max(0.0, dot(H, N)), phong_exp), F) / M_PI;
}

