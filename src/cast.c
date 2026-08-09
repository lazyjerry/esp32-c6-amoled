#include "cast.h"

#include "esp_random.h"

// 立筊機率設在十萬分之一（0.001%）；剩下的照兩片各自正反的組合分：
// 一平一凸有兩種排列所以是一半，兩片同面各四分之一。
#define LI_ODDS 100000

cast_result_t cast_draw(void)
{
    if (esp_random() % LI_ODDS == 0) return CAST_LI;

    switch (esp_random() % 4) {
        case 0:
        case 1:  return CAST_SHENG;
        case 2:  return CAST_XIAO;
        default: return CAST_YIN;
    }
}

const char *cast_result_name(cast_result_t r)
{
    switch (r) {
        case CAST_SHENG: return "聖筊";
        case CAST_XIAO:  return "笑筊";
        case CAST_YIN:   return "陰筊";
        default:         return "立筊";
    }
}

const char *cast_result_desc(cast_result_t r)
{
    switch (r) {
        case CAST_SHENG: return "神明允可，依此而行";
        case CAST_XIAO:  return "神明一笑，所問不明，請再問過";
        case CAST_YIN:   return "神明不允，此事不宜";
        default:         return "極為罕見，宜慎重再問";
    }
}
