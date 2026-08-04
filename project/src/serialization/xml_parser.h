#ifndef XML_PARSER_H
#define XML_PARSER_H

#include "parser.h"

namespace krn
{
    class XML_Parser final : public Parser
    {
        public:

        ~XML_Parser() override = default;

        std::optional<SceneDesc> ParseScene(fs::path filepath) override;
    };
}

#endif