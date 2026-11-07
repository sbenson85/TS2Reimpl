#include "hook/hooks.h"
#include "gz/ExpressionEvaluator.h"
#include "rz/RZStringSearch.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0b0a870", 0xb0a870, 32, 0xe92fdd05, MemberAddress(&nGZCommandParser::ExpressionEvaluator::Evaluate)},
    {"FUN_a0b09940", 0xb09940, 32, 0x9c784396, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseExpression)},
    {"FUN_a0b097e0", 0xb097e0, 32, 0xf73435c8, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseSequence)},
    {"FUN_a0b099e0", 0xb099e0, 32, 0x7f0ae823, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseLogical)},
    {"FUN_a0b0a200", 0xb0a200, 32, 0x9b8c425e, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseComparison)},
    {"FUN_a0b09740", 0xb09740, 32, 0x62d6e187, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseSum)},
    {"FUN_a0b09c50", 0xb09c50, 32, 0x6980043c, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseProduct)},
    {"FUN_a0b0a660", 0xb0a660, 32, 0x043fbfa0, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParseUnary)},
    {"FUN_a0b09f50", 0xb09f50, 32, 0x3df695ce, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ParsePrimary)},
    {"nGZCommandParser_cBaseParser_ReadQuotedName", 0xb0a580, 32, 0x5565bafb, MemberAddress(&nGZCommandParser::ExpressionEvaluator::ReadQuotedName)},
    {"AUcEvalTrue_A0x293d2d98_vf0", 0xb07bb0, 24, 0xc87da018, MemberAddress(&nGZCommandParser::cEvalTrue::Evaluate)},
    {"AUcEvalFalse_A0x293d2d98_vf0", 0xb07bd0, 20, 0x9e06282f, MemberAddress(&nGZCommandParser::cEvalFalse::Evaluate)},
    {"AUcEvalPI_A0x293d2d98_vf0", 0xb07bf0, 24, 0x33f64a66, MemberAddress(&nGZCommandParser::cEvalPI::Evaluate)},
    {"AUcEvalRnd_A0x293d2d98_vf0", 0xb07c10, 32, 0x8bd20e53, MemberAddress(&nGZCommandParser::cEvalRnd::Evaluate)},
    {"AUcEvalExp_A0x293d2d98_vf0", 0xb07d20, 32, 0xf09655a0, MemberAddress(&nGZCommandParser::cEvalExp::Evaluate)},
    {"AUcEvalLog_A0x293d2d98_vf0", 0xb07d90, 32, 0xdd30b2bf, MemberAddress(&nGZCommandParser::cEvalLog::Evaluate)},
    {"AUcEvalAbs_A0x293d2d98_vf0", 0xb07e00, 32, 0x5f3ad66f, MemberAddress(&nGZCommandParser::cEvalAbs::Evaluate)},
    {"AUcEvalSin_A0x293d2d98_vf0", 0xb07e70, 32, 0xfa289671, MemberAddress(&nGZCommandParser::cEvalSin::Evaluate)},
    {"AUcEvalCos_A0x293d2d98_vf0", 0xb07ee0, 32, 0x953bf437, MemberAddress(&nGZCommandParser::cEvalCos::Evaluate)},
    {"AUcEvalTan_A0x293d2d98_vf0", 0xb07f50, 32, 0x72855b6b, MemberAddress(&nGZCommandParser::cEvalTan::Evaluate)},
    {"AUcEvalSqr_A0x293d2d98_vf0", 0xb07fc0, 32, 0xcec2045b, MemberAddress(&nGZCommandParser::cEvalSqr::Evaluate)},
    {"AUcEvalPow_A0x293d2d98_vf0", 0xb08090, 32, 0x89ffa5c8, MemberAddress(&nGZCommandParser::cEvalPow::Evaluate)},
    {"AUcEvalSqrt_A0x293d2d98_vf0", 0xb08110, 32, 0xf6ad5ab4, MemberAddress(&nGZCommandParser::cEvalSqrt::Evaluate)},
    {"AUcEvalCeil_A0x293d2d98_vf0", 0xb08180, 32, 0x965819ed, MemberAddress(&nGZCommandParser::cEvalCeil::Evaluate)},
    {"AUcEvalAsin_A0x293d2d98_vf0", 0xb081f0, 32, 0x4f3653f4, MemberAddress(&nGZCommandParser::cEvalAsin::Evaluate)},
    {"AUcEvalAcos_A0x293d2d98_vf0", 0xb08260, 32, 0x45a170d2, MemberAddress(&nGZCommandParser::cEvalAcos::Evaluate)},
    {"AUcEvalAtan_A0x293d2d98_vf0", 0xb082d0, 32, 0x68762ade, MemberAddress(&nGZCommandParser::cEvalAtan::Evaluate)},
    {"AUcEvalFloor_A0x293d2d98_vf0", 0xb08340, 32, 0x9685bb28, MemberAddress(&nGZCommandParser::cEvalFloor::Evaluate)},
    {"AUcEvalAtan2_A0x293d2d98_vf0", 0xb083b0, 32, 0x5ca270eb, MemberAddress(&nGZCommandParser::cEvalAtan2::Evaluate)},
    {"AUcEvalStrcmp_A0x293d2d98_vf0", 0xb08450, 32, 0xefba9074, MemberAddress(&nGZCommandParser::cEvalStrcmp::Evaluate)},
    {"AUcEvalStrstr_A0x293d2d98_vf0", 0xb08510, 32, 0x0cf58f98, MemberAddress(&nGZCommandParser::cEvalStrstr::Evaluate)},
    {"AUcEvalRadiansToTurns_A0x293d2d98_vf0", 0xb086d0, 32, 0x0f8c02e6, MemberAddress(&nGZCommandParser::cEvalRadiansToTurns::Evaluate)},
    {"AUcEvalDegreesToTurns_A0x293d2d98_vf0", 0xb08730, 32, 0xb2d63b32, MemberAddress(&nGZCommandParser::cEvalDegreesToTurns::Evaluate)},
    {"AUcEvalRadiansToDegrees_A0x293d2d98_vf0", 0xb08790, 32, 0xa1f894d7, MemberAddress(&nGZCommandParser::cEvalRadiansToDegrees::Evaluate)},
    {"AUcEvalDegreesToRadians_A0x293d2d98_vf0", 0xb08860, 32, 0x0b6519d9, MemberAddress(&nGZCommandParser::cEvalDegreesToRadians::Evaluate)},
    {"nGZCommandParser_ExpectChar", 0xb096d0, 32, 0xaa422bf3, reinterpret_cast<void *>(&nGZCommandParser::ExpectChar)},
    {"FUN_a0b09380", 0xb09380, 32, 0x47d15730, reinterpret_cast<void *>(&nGZCommandParser::Compare3)},
    {"FUN_a0b09660", 0xb09660, 28, 0x0a3bb0a7, reinterpret_cast<void *>(&nGZCommandParser::IsNearZero)},
    {"FUN_a0b09680", 0xb09680, 32, 0x5967bbff, reinterpret_cast<void *>(&nGZCommandParser::IsTrue)},
    {"FUN_a0b09400", 0xb09400, 32, 0x99e37fa2, reinterpret_cast<void *>(&nGZCommandParser::ScaleRange)},
    {"FUN_a01b1a52", 0x1b1a52, 32, 0x822f689c, reinterpret_cast<void *>(&rztext::FindNoCase)},
};
REGISTER_HOOKS(kHooks);

} // namespace
