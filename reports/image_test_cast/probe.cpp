#include <VisionAlgorithm/VisionAlgorithm.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <variant>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const cv::Mat original = cv::imread(argv[1], cv::IMREAD_UNCHANGED);
    if (original.empty()) return 3;
    cv::Mat gray;
    if (original.type() == CV_8UC3) cv::cvtColor(original, gray, cv::COLOR_BGR2GRAY);
    else if (original.type() == CV_8UC4) cv::cvtColor(original, gray, cv::COLOR_BGRA2GRAY);
    else if (original.type() == CV_8UC1) gray = original.clone();
    else return 4;
    if (!gray.isContinuous()) gray = gray.clone();
    const auto before = gray.clone();
    const std::filesystem::path outputDir(argv[3]);
    std::filesystem::create_directories(outputDir);
    auto store = std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    auto created = vision::algorithm::VisionAlgorithm::create(argv[2], store);
    auto reportError = [](const vision::core::Error& e) {
        std::cout << "ERROR code=" << static_cast<int>(e.code) << " message=" << e.message
                  << " diagnostic=" << e.diagnostic << '\n';
    };
    if (const auto* e = std::get_if<vision::core::Error>(&created)) { reportError(*e); return 5; }
    auto& algorithm = std::get<std::unique_ptr<vision::core::ILocateAlgorithm>>(created);
    std::ofstream report(outputDir / "results.json");
    report << std::setprecision(15) << "{\n\"image_width\":" << gray.cols << ",\"image_height\":" << gray.rows
           << ",\"source_channels\":" << original.channels() << ",\"calibration\":\"identity_pixel_only\",\n\"runs\":[\n";
    vision::core::AlgorithmOutput first{};
    vision::algorithm::DemoEdgeVisualization snap;
    bool same = true;
    for (int i = 0; i < 10; ++i) {
        const auto start = std::chrono::steady_clock::now();
        auto outcome = algorithm->locate(vision::core::EdgeAlgorithmInput{std::cref(gray)});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (const auto* e = std::get_if<vision::core::Error>(&outcome)) { reportError(*e); return 6; }
        const auto result = std::get<vision::core::AlgorithmOutput>(outcome);
        if (i == 0) { first = result; snap = store->snapshot(); }
        same = same && result.x == first.x && result.y == first.y && result.r == first.r && result.valid == first.valid;
        if (i) report << ",\n";
        report << "{\"valid\":" << (result.valid ? "true" : "false") << ",\"x\":" << result.x
               << ",\"y\":" << result.y << ",\"r\":" << result.r << ",\"elapsed_ms\":" << ms << '}';
        std::cout << std::setprecision(12) << "run=" << i+1 << " valid=" << result.valid << " X=" << result.x
                  << " Y=" << result.y << " R=" << result.r << " elapsed_ms=" << ms << '\n';
    }
    const bool unchanged = cv::norm(gray, before, cv::NORM_INF) == 0;
    cv::Mat blank(gray.size(), CV_8UC1, cv::Scalar(230));
    auto negative = algorithm->locate(vision::core::EdgeAlgorithmInput{std::cref(blank)});
    const auto* negativeResult = std::get_if<vision::core::AlgorithmOutput>(&negative);
    const bool blankRejected = negativeResult && !negativeResult->valid;
    report << "\n],\n\"identical_repeated_outputs\":" << (same ? "true":"false")
           << ",\"input_unchanged\":" << (unchanged ? "true":"false")
           << ",\"blank_control_returns_ng\":" << (blankRejected ? "true":"false") << ",\n\"corners\":[";
    for (size_t i=0; i<snap.corners.size(); ++i) {
        if(i) report << ',';
        report << '[' << snap.corners[i].x << ',' << snap.corners[i].y << ']';
    }
    report << "]\n}\n";
    std::cout << "input_unchanged=" << unchanged << " identical_repeated_outputs=" << same
              << " blank_control_returns_ng=" << blankRejected << " fitted_lines=" << snap.fittedLines.size()
              << " corners=" << snap.corners.size() << '\n';
    if (!snap.rectifiedImage.empty()) {
        cv::Mat annotated;
        cv::cvtColor(snap.rectifiedImage, annotated, cv::COLOR_GRAY2BGR);
        auto point=[](const vision::algorithm::DemoOverlayPoint& p) { return cv::Point(cvRound(p.x),cvRound(p.y)); };
        for(const auto& roi:snap.rois) for(int j=0;j<4;++j)
            cv::line(annotated,point(roi.corners[j]),point(roi.corners[(j+1)%4]),cv::Scalar(255,210,0),1,cv::LINE_AA);
        for(const auto& line:snap.fittedLines) {
            auto a=point(line.start), b=point(line.end);
            if(cv::clipLine(annotated.size(),a,b)) cv::line(annotated,a,b,cv::Scalar(60,240,60),2,cv::LINE_AA);
        }
        for(size_t i=0;i<snap.corners.size();++i) {
            cv::circle(annotated,point(snap.corners[i]),6,cv::Scalar(0,180,255),2,cv::LINE_AA);
            cv::putText(annotated,std::to_string(i+1),point(snap.corners[i])+cv::Point(9,-9),cv::FONT_HERSHEY_SIMPLEX,0.65,cv::Scalar(0,180,255),2,cv::LINE_AA);
        }
        if(first.valid) cv::drawMarker(annotated,cv::Point(cvRound(first.x),cvRound(first.y)),cv::Scalar(255,80,255),cv::MARKER_CROSS,32,2,cv::LINE_AA);
        cv::Mat canvas;
        cv::copyMakeBorder(annotated,canvas,84,48,0,0,cv::BORDER_CONSTANT,cv::Scalar(25,30,38));
        std::ostringstream title;
        title << std::fixed << std::setprecision(3) << "REAL EDGE ALGORITHM | " << (first.valid?"VALID":"NG")
              << " | X=" << first.x << " px  Y=" << first.y << " px  R=" << first.r << " deg";
        cv::putText(canvas,title.str(),{20,32},cv::FONT_HERSHEY_SIMPLEX,0.68,cv::Scalar(245,245,245),1,cv::LINE_AA);
        cv::putText(canvas,"test_cast_.png | Identity calibration; offline image test; no physical XY calibration",{20,64},cv::FONT_HERSHEY_SIMPLEX,0.59,cv::Scalar(190,200,215),1,cv::LINE_AA);
        cv::putText(canvas,"Cyan: configured ROIs   Green: fitted edges   Orange: corners   Magenta: center",{20,canvas.rows-18},cv::FONT_HERSHEY_SIMPLEX,0.62,cv::Scalar(230,235,240),1,cv::LINE_AA);
        cv::imwrite((outputDir/"edge_result.png").string(),canvas);
    }
    return same && unchanged && blankRejected && first.valid ? 0 : 7;
}
