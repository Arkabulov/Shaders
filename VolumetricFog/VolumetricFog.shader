{
    "Source": "VolumetricFog.azsl",

    "DepthStencilState":
    {
        "Depth":
        {
            "Enable": false
        },
        "Stencil":
        {
            "Enable": false
        }
    },

    "DrawList": "forward",

    "ProgramSettings":
    {
        "EntryPoints":
        [
            {
                "name": "MainVS",
                "type": "Vertex"
            },
            {
                "name": "MainPS",
                "type": "Fragment"
            }
        ]
    }
}
