$input a_position

#include "../common.sh"

uniform vec2 viewSize;

void main()
{
	gl_Position = vec4(2.0 * a_position.x/viewSize.x - 1.0, 1.0 - 2.0 * a_position.y/viewSize.y, 0.0, 1.0);
	//v_color0 = a_color0;
}

