-- Vertex

void main(void)
{

    gl_Position = vec4(inVertexData,1.0);
}


-- Fragment

layout(location = 0) out vec4 _colourOut;

void main(void){
    _colourOut = vec4(1.0);
}
