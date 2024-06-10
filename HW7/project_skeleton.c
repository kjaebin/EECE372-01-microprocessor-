#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include <arm_neon.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wiringPi.h>
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#define CLOCKS_PER_US ((double)CLOCKS_PER_SEC / 1000000)
#define CLASS 10

// Input dim
#define I1_C 1
#define I1_H 28
#define I1_W 28

// Conv1 out dim
#define I2_C 16
#define I2_H 14
#define I2_W 14

// Conv2 out dim
#define I3_C 1
#define I3_H 14
#define I3_W 14

#define CONV1_KERNAL 3
#define CONV1_STRIDE 2
#define CONV2_KERNAL 3
#define CONV2_STRIDE 1
#define FC_IN (I2_H * I2_W)
#define FC_OUT CLASS

typedef struct _model {
    float conv1_weight[I2_C * I1_C * CONV1_KERNAL * CONV1_KERNAL];
    float conv1_bias[I2_C];
    float conv2_weight[I3_C * I2_C * CONV2_KERNAL * CONV2_KERNAL];
    float conv2_bias[I3_C];
    float fc_weight[FC_OUT * FC_IN];
    float fc_bias[FC_OUT];
} model;

int pin_num[] = {29, 28, 23, 22, 21, 27, 26};

void resize_280_to_28(unsigned char *in, unsigned char *out);
void Gray_scale(unsigned char *feature_in, unsigned char *feature_out);
void Normalized(unsigned char *feature_in, float *feature_out);
void Padding(float *feature_in, float *feature_out, int C, int H, int W);
void Conv_2d(float *feature_in, float *feature_out, int in_C, int in_H, int in_W, int out_C, int out_H, int out_W, int K, int S, float *weight, float *bias);
void ReLU(float *feature_in, int elem_num);
void Linear(float *feature_in, float *feature_out, float *weight, float *bias);
void Log_softmax(float *activation);
int Get_pred(float *activation);
void Get_CAM(float *activation, float *cam, int pred, float *weight);
void save_image(float *feature_scaled, float *cam);
void setup_gpio();
void display_number(int number);

int main(int argc, char *argv[]) {
    clock_t start1, end1, start2, end2;

    model net;
    FILE *weights = fopen("./weights.bin", "rb");
    if (weights == NULL) {
        printf("Error opening weights file.\n");
        return -1;
    }
    size_t read_size = fread(&net, sizeof(model), 1, weights);
    fclose(weights);

    if (read_size != 1) {
        printf("Error reading weights file.\n");
        return -1;
    }

    // Print some of the weights for debugging
    printf("conv1_weight[0]: %f\n", net.conv1_weight[0]);
    printf("conv1_bias[0]: %f\n", net.conv1_bias[0]);
    printf("fc_weight[0]: %f\n", net.fc_weight[0]);
    printf("fc_bias[0]: %f\n", net.fc_bias[0]);

    char *file;
    if (atoi(argv[1]) == 0) {
        system("libcamera-still -e bmp --width 280 --height 280 -t 20000 -o image.bmp");
        file = "image.bmp";
    } else if (atoi(argv[1]) == 1) {
        file = "example_1.bmp";
    } else if (atoi(argv[1]) == 2) {
        file = "example_2.bmp";
    } else {
        printf("Wrong Input!\n");
        exit(1);
    }

    unsigned char *feature_in;
    unsigned char *feature_resize;
    unsigned char feature_gray[I1_C * I1_H * I1_W];
    float feature_scaled[I1_C * I1_H * I1_W];
    float feature_padding1[I1_C * (I1_H + 2) * (I1_W + 2)];
    float feature_conv1_out[I2_C * I2_H * I2_W];
    float feature_padding2[I2_C * (I2_H + 2) * (I2_W + 2)];
    float feature_conv2_out[I3_C * I3_H * I3_W];
    float fc_out[1 * CLASS];
    float cam[1 * I3_H * I3_W];
    int channels, height, width;

    if (atoi(argv[1]) == 0) {
        feature_resize = stbi_load(file, &width, &height, &channels, 3);
        if (feature_resize == NULL) {
            printf("Failed to load image: %s\n", file);
            return -1;
        }
        feature_in = (unsigned char *)malloc(sizeof(unsigned char) * 3 * I1_H * I1_W);
        resize_280_to_28(feature_resize, feature_in);
    } else {
        feature_in = stbi_load(file, &width, &height, &channels, 3);
        if (feature_in == NULL) {
            printf("Failed to load image: %s\n", file);
            return -1;
        }
    }

    int pred = 0;
    Gray_scale(feature_in, feature_gray);
    Normalized(feature_gray, feature_scaled);

    clock_t start_padding, end_padding, start_conv, end_conv, start_relu, end_relu, start_fc, end_fc, start_cam, end_cam;
    double time_padding, time_conv, time_relu, time_fc, time_cam, total_time;

    start_padding = clock();
    Padding(feature_scaled, feature_padding1, I1_C, I1_H, I1_W);
    end_padding = clock();
    time_padding = (double)(end_padding - start_padding) / CLOCKS_PER_US;

    start_conv = clock();
    Conv_2d(feature_padding1, feature_conv1_out, I1_C, I1_H + 2, I1_W + 2, I2_C, I2_H, I2_W, CONV1_KERNAL, CONV1_STRIDE, net.conv1_weight, net.conv1_bias);
    Conv_2d(feature_conv1_out, feature_conv2_out, I2_C, I2_H, I2_W, I3_C, I3_H, I3_W, CONV2_KERNAL, CONV2_STRIDE, net.conv2_weight, net.conv2_bias);
    end_conv = clock();
    time_conv = (double)(end_conv - start_conv) / CLOCKS_PER_US;

    start_relu = clock();
    ReLU(feature_conv1_out, I2_C * I2_H * I2_W);
    ReLU(feature_conv2_out, I3_C * I3_H * I3_W);
    end_relu = clock();
    time_relu = (double)(end_relu - start_relu) / CLOCKS_PER_US;

    start_fc = clock();
    Linear(feature_conv2_out, fc_out, net.fc_weight, net.fc_bias);
    end_fc = clock();
    time_fc = (double)(end_fc - start_fc) / CLOCKS_PER_US;

    total_time = time_padding + time_conv + time_relu + time_fc;

    Log_softmax(fc_out);

    start_cam = clock();
    pred = Get_pred(fc_out);
    Get_CAM(feature_conv2_out, cam, pred, net.fc_weight);
    end_cam = clock();
    time_cam = (double)(end_cam - start_cam) / CLOCKS_PER_US;

    save_image(feature_scaled, cam);

    setup_gpio();
    display_number(pred);

    printf("Zero Padding time: %9.3lf[us]\n", time_padding);
    printf("Conv time: %9.3lf[us]\n", time_conv);
    printf("ReLU time: %9.3lf[us]\n", time_relu);
    printf("FC time: %9.3lf[us]\n", time_fc);
    printf("Total time (excluding Softmax): %9.3lf[us]\n", total_time);
    printf("CAM time: %9.3lf[us]\n", time_cam);
    printf("Total time (including CAM): %9.3lf[us]\n", total_time + time_cam);

    printf("Log softmax value\n");
    for (int i = 0; i < CLASS; i++) {
        printf("%2d: %6.3f\n", i, fc_out[i]);
    }
    printf("Prediction: %d\n", pred);

    if (atoi(argv[1]) == 0) {
        free(feature_in);
        stbi_image_free(feature_resize);
    } else {
        stbi_image_free(feature_in);
    }
    return 0;
}

void resize_280_to_28(unsigned char *in, unsigned char *out) {
    int x, y, c;
    for (y = 0; y < 28; y++) {
        for (x = 0; x < 28; x++) {
            for (c = 0; c < 3; c++) {
                out[y * 28 * 3 + x * 3 + c] = in[y * 10 * 280 * 3 + x * 10 * 3 + c];
            }
        }
    }
    return;
}

void Gray_scale(unsigned char *feature_in, unsigned char *feature_out) {
    for (int h = 0; h < I1_H; h++) {
        for (int w = 0; w < I1_W; w++) {
            int sum = 0;
            for (int c = 0; c < 3; c++) {
                sum += feature_in[I1_H * 3 * h + 3 * w + c];
            }
            feature_out[I1_W * h + w] = sum / 3;
        }
    }
    return;
}

void Normalized(unsigned char *feature_in, float *feature_out) {
    for (int i = 0; i < I1_H * I1_W; i++) {
        feature_out[i] = ((float)feature_in[i]) / 255.0;
    }
    return;
}

void Padding(float *feature_in, float *feature_out, int C, int H, int W) {
    float32x4_t zero_vector = vdupq_n_f32(0.0);
    for (int c = 0; c < C; c++) {
        for (int i = 0; i < (W + 2) / 4; i++) {
            vst1q_f32(&feature_out[c * (H + 2) * (W + 2) + 0 * (W + 2) + i * 4], zero_vector);
            vst1q_f32(&feature_out[c * (H + 2) * (W + 2) + (H + 1) * (W + 2) + i * 4], zero_vector);
        }
        for (int h = 1; h <= H; h++) {
            feature_out[c * (H + 2) * (W + 2) + h * (W + 2) + 0] = 0;
            feature_out[c * (H + 2) * (W + 2) + h * (W + 2) + (W + 1)] = 0;
            for (int w = 1; w <= W; w += 4) {
                float32x4_t neon_in = vld1q_f32(&feature_in[c * H * W + (h - 1) * W + (w - 1)]);
                vst1q_f32(&feature_out[c * (H + 2) * (W + 2) + h * (W + 2) + w], neon_in);
            }
        }
    }
}

void Conv_2d(float *feature_in, float *feature_out, int in_C, int in_H, int in_W, int out_C, int out_H, int out_W, int K, int S, float *weight, float *bias) {
    for (int oc = 0; oc < out_C; oc++) {
        for (int oh = 0; oh < out_H; oh++) {
            for (int ow = 0; ow < out_W; ow++) {
                float32x4_t partial_sum = vdupq_n_f32(0.0f);
                for (int ic = 0; ic < in_C; ic++) {
                    for (int kh = 0; kh < K; kh++) {
                        int ih = oh * S + kh;
                        for (int kw = 0; kw < K; kw += 4) {
                            int iw = ow * S + kw;
                            float32x4_t in_value = vld1q_f32(&feature_in[ic * in_H * in_W + ih * in_W + iw]);
                            float32x4_t weight_value = vld1q_f32(&weight[oc * in_C * K * K + ic * K * K + kh * K + kw]);
                            partial_sum = vmlaq_f32(partial_sum, in_value, weight_value);
                        }
                    }
                }
                float result = vaddvq_f32(partial_sum);
                feature_out[oc * out_H * out_W + oh * out_W + ow] = result + bias[oc];
            }
        }
    }
}

void ReLU(float *feature_in, int elem_num) {
    float32x4_t zero_vector = vdupq_n_f32(0.0f);
    int i;
    for (i = 0; i < elem_num; i += 4) {
        float32x4_t in_vector = vld1q_f32(&feature_in[i]);
        uint32x4_t condition = vcltq_f32(in_vector, zero_vector);
        float32x4_t result = vbslq_f32(condition, zero_vector, in_vector);
        vst1q_f32(&feature_in[i], result);
    }
    for (; i < elem_num; i++) {
        if (feature_in[i] < 0) {
            feature_in[i] = 0;
        }
    }
}

void Linear(float *feature_in, float *feature_out, float *weight, float *bias) {
    for (int i = 0; i < CLASS; i++) {
        float32x4_t partial_sum = vdupq_n_f32(0.0f);
        for (int j = 0; j < I3_H * I3_W; j += 4) {
            float32x4_t in_vector = vld1q_f32(&feature_in[j]);
            float32x4_t weight_vector = vld1q_f32(&weight[i * I3_H * I3_W + j]);
            partial_sum = vmlaq_f32(partial_sum, in_vector, weight_vector);
        }
        float sum[4];
        vst1q_f32(sum, partial_sum);
        feature_out[i] = sum[0] + sum[1] + sum[2] + sum[3] + bias[i];
    }
}

void Log_softmax(float *activation) {
    double max = activation[0];
    double sum = 0.0;
    for (int i = 1; i < CLASS; i++) {
        if (activation[i] > max) {
            max = activation[i];
        }
    }
    for (int i = 0; i < CLASS; i++) {
        activation[i] = exp(activation[i] - max);
        sum += activation[i];
    }
    for (int i = 0; i < CLASS; i++) {
        activation[i] = log(activation[i] / sum);
    }
}

int Get_pred(float *activation) {
    int pred = 0;
    float max_val = activation[0];
    for (int i = 1; i < CLASS; i++) {
        if (activation[i] > max_val) {
            max_val = activation[i];
            pred = i;
        }
    }
    return pred;
}

void Get_CAM(float *activation, float *cam, int pred, float *weight) {
    for (int i = 0; i < I3_H * I3_W; i++) {
        cam[i] = 0;
        for (int j = 0; j < I3_H; j++) {
            cam[i] += activation[i + j * I3_H * I3_W] * weight[pred * I3_H * I3_W + i + j * I3_H * I3_W];
        }
    }
}

void save_image(float *feature_scaled, float *cam) {
    float *output = (float *)malloc(sizeof(float) * 3 * I1_H * I1_W);
    unsigned char *output_bmp = (unsigned char *)malloc(sizeof(unsigned char) * 3 * I1_H * I1_W);
    unsigned char *output_bmp_resized = (unsigned char *)malloc(sizeof(unsigned char) * 3 * I1_H * 14 * I1_W * 14);

    float min = cam[0];
    float max = cam[0];
    for (int i = 1; i < I3_H * I3_W; i++) {
        if (cam[i] < min) {
            min = cam[i];
        }
        if (cam[i] > max) {
            max = cam[i];
        }
    }

    for (int h = 0; h < I1_H; h++) {
        for (int w = 0; w < I1_W; w++) {
            for (int c = 0; c < 3; c++) {
                output[I1_H * I1_W * c + I1_W * h + w] = (cam[I3_W * (h >> 1) + (w >> 1)] - min) / (max - min);
            }
        }
    }

    for (int h = 0; h < I1_H; h++) {
        for (int w = 0; w < I1_W; w++) {
            for (int c = 0; c < 3; c++) {
                output_bmp[I1_H * 3 * h + 3 * w + c] = (output[I1_H * I1_W * c + I1_W * h + w]) * 255;
            }
        }
    }

    stbir_resize_uint8_linear(output_bmp, I1_H, I1_W, 0, output_bmp_resized, I1_H * 14, I1_W * 14, 0, 3);
    stbi_write_bmp("Activation_map.bmp", I1_W * 14, I1_H * 14, 3, output_bmp_resized);

    free(output);
    free(output_bmp);
}

void setup_gpio() {
    if (wiringPiSetup() == -1) {
        printf("GPIO setup failed\n");
        exit(1);
    }
    for (int i = 0; i < 7; i++) {
        pinMode(pin_num[i], OUTPUT);
    }
}

void display_number(int number) {
    int hex_table[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, // 0
        {0, 1, 1, 0, 0, 0, 0}, // 1
        {1, 1, 0, 1, 1, 0, 1}, // 2
        {1, 1, 1, 1, 0, 0, 1}, // 3
        {0, 1, 1, 0, 0, 1, 1}, // 4
        {1, 0, 1, 1, 0, 1, 1}, // 5
        {1, 0, 1, 1, 1, 1, 1}, // 6
        {1, 1, 1, 0, 0, 0, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8
        {1, 1, 1, 1, 0, 1, 1}  // 9
    };
    for (int i = 0; i < 7; i++) {
        digitalWrite(pin_num[i], hex_table[number][i]);
    }
}
