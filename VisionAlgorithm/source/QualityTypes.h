#pragma once

namespace vision::algorithm::detail {

    /*
        算法质量判定使用的闭区间。

        是否允许负数由具体配置字段的解析规则决定；结构本身只负责承载
        已经校验过的上下限。
    */
    struct MeasurementRangeConfig {
        double lower{0.0};
        double upper{0.0};
    };

} // namespace vision::algorithm::detail
