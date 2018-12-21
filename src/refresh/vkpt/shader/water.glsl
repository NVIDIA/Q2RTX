/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Johannes Hanika

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


float hash1( vec2 p )
{
	p  = 50.0*fract( p*0.3183099 );
	return fract( p.x*p.y*(p.x+p.y) );
}
float noise( vec2 x )
{
	vec2 p = floor(x);
	vec2 w = fract(x);
	vec2 u = w*w*w*(w*(w*6.0-15.0)+10.0);

	float a = hash1(p+vec2(0,0));
	float b = hash1(p+vec2(1,0));
	float c = hash1(p+vec2(0,1));
	float d = hash1(p+vec2(1,1));

	return -1.0+2.0*( a + (b-a)*u.x + (c-a)*u.y + (a - b - c + d)*u.x*u.y );
}

const int ITER_FRAGMENT = 4;
const float SEA_HEIGHT = 5.0;
const float SEA_CHOPPY = 3.0;
const float SEA_SPEED = 0.15;
const float SEA_FREQ = 0.02;//0.16;
#define SEA_TIME (1.0f + 4.0 * float(global_ubo.time))
//#define SEA_TIME (1.0f + time * SEA_SPEED)
// #define SEA_TIME 1.0 // static sea
float sea_octave(vec2 uv, float choppy)
{
	uv += noise(uv);        
	vec2 wv = 1.0-abs(sin(uv));
	vec2 swv = abs(cos(uv));    
	wv = mix(wv,swv,wv);
	return pow(1.0-pow(wv.x * wv.y,0.65),choppy);
}

const mat2 octave_m = mat2(1.6,1.2,-1.2,1.6);
float water(vec2 p)
{
	float freq = SEA_FREQ;
	float amp = SEA_HEIGHT;
	float choppy = SEA_CHOPPY;
	vec2 uv = p.xy; uv.x *= 0.75;

	float d, h = 0.0;    
	for(int i = 0; i < ITER_FRAGMENT; i++)
	{
		d = sea_octave((uv+SEA_TIME)*freq,choppy);
		// d += sea_octave((uv-SEA_TIME)*freq,choppy);
		h += d * amp;        
		uv *= octave_m; freq *= 1.9; amp *= 0.22;
		choppy = mix(choppy,1.0,0.2);
	}
	return h;
}
vec3 waterd( in vec2 p )
{
	float eps = 0.01;
	vec3 n;
	n.y = water(p);    
	n.x = water(vec2(p.x+eps,p.y)) - n.y;
	n.z = water(vec2(p.x,p.y+eps)) - n.y;
	n.y = eps * 10.0;
	n = normalize(n);
	return n;
}

mat2 makem2(in float theta){float c = cos(theta);float s = sin(theta);return mat2(c,-s,s,c);}

/* https://www.shadertoy.com/view/lslXRS */
vec2 gradn(vec2 p)
{
	float ep = .09;
	float gradx = noise(vec2(p.x+ep,p.y))-noise(vec2(p.x-ep,p.y));
	float grady = noise(vec2(p.x,p.y+ep))-noise(vec2(p.x,p.y-ep));
	return vec2(gradx,grady);
}

vec3
lava (in vec2 p)
{
	float z=2.;
	float rz = 0.;
	vec2 bp = p;
	float time = global_ubo.time * 0.06;
	for (float i= 1.;i < 7.;i++ )
	{
		//primary flow speed
		p += time * .6;
		
		//secondary flow speed (speed of the perceived flow)
		bp += time * 1.9;
		
		//displacement field (try changing time multiplier)
		vec2 gr = gradn(i*p*.34+ time);
		
		//rotation of the displacement field
		gr*=makem2(time * 6.-(0.05*p.x+0.03*p.y)*40.);
		
		//displace the system
		p += gr*.5;
		
		//add noise octave
		rz+= (sin(noise(p)*7.)*0.5+0.5)/z;
		
		//blend factor (blending displaced system with base system)
		//you could call this advection factor (.5 being low, .95 being high)
		p = mix(bp,p,.77);
		
		//intensity scaling
		z *= 1.4;
		//octave scaling
		p *= 2.;
		bp *= 1.9;
	}
	return pow(vec3(.2,0.07,0.01) / rz, vec3(1.4 * 2.4)) * 5.0;	
}
